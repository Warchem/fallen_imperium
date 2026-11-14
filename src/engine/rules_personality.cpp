// Computer-player personalities in the rules (0.12.0): setting them up, the
// per-pair B-Factor (betrayal factor), the 8-turn d100 that arms a betrayal,
// hidden betrayal objectives and when they count as done. Everything here is
// hidden information except the archetypes, revealed at the end of round 3.
// The AI side (how each personality plays) is in ai/ai_style.cpp.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool debugBetrayal() { return std::getenv("TI4_BETRAYDEBUG") != nullptr; }

float sideWorth(const Game& g, const DealSide& s) {
    float v = float(s.tg + s.comm);
    if (s.note >= 0) v += noteDefaultValue(g, s.note);
    v += float(s.fragments.size()) + 2.f * float(s.actionCards.size()) + float(s.captured.size());
    if (s.planet >= 0) v += 3.f;
    return v;
}
}  // namespace

void Rules::setupPersonalities() {
    const Setup& s = E.setup();
    G.seed = s.seed;
    for (int p = 0; p < G.numPlayers; ++p) {
        Player& pl = G.players[p];
        pl.pers = Personality();
        if (pl.human) continue;  // humans have no personality (and no B-Factor)
        int want = p < int(s.archetypes.size()) ? s.archetypes[p] : -1;
        if (want == -2) continue;  // test only: the plain 0.11 computer player
        Personality& pe = pl.pers;
        if (want >= 0 && want < int(Arch::Count)) {
            pe.arch = int8_t(want);
        } else if ((s.extras & 4) && (pl.aiLevel >= 0 ? pl.aiLevel : G.aiLevel) >= 3) {
            // 0.27.0 (owner): a Competitive bot left to chance is never a Rusher or Chaotic -
            // both can still be chosen by hand (Setup::extras bit 2; older saves roll as before)
            static const Arch kPool[] = {Arch::Politician, Arch::Peacekeeper, Arch::Warlord, Arch::Betrayer,
                                         Arch::Swindler,   Arch::Merchant,    Arch::Opportunist, Arch::Turtler};
            pe.arch = int8_t(kPool[prand(G, 1, p, 0, 0, uint32_t(sizeof kPool / sizeof kPool[0]))]);
        } else {
            pe.arch = int8_t(prand(G, 1, p, 0, 0, uint32_t(Arch::Count)));
        }
        pe.evo = int8_t(prand(G, 2, p, 0, 0, uint32_t(Evo::Count)));
        pe.quirk = int8_t(prand(G, 3, p, 0, 0, uint32_t(Quirk::Count)));
        pe.fquirk = int8_t(prand(G, 4, p, 0, 0, 2));
        if (const char* fq = std::getenv("TI4_SIMFQUIRK")) {  // (test only: every bot's faction trait - or one seat's)
            const char* seat = std::getenv("TI4_SIMFQUIRKSEAT");
            if (!seat || std::atoi(seat) == p) pe.fquirk = int8_t(std::atoi(fq) & 1);
        }
        pe.now = pe.arch == int8_t(Arch::Chaotic) ? int8_t(prand(G, 5, p, 0, 0, uint32_t(Arch::Chaotic))) : pe.arch;
        for (int q = 0; q < G.numPlayers && q < 8; ++q) pe.bf[q] = q == p ? 0.f : kArchetypes[pe.arch].bStart;
    }
}

// ------------------------------------------------------------------ B-Factor
void Rules::bfChange(int bot, int toward, float amount) {
    if (!isBot(G, bot) || toward < 0 || toward >= G.numPlayers || toward == bot || toward >= 8) return;
    Personality& pe = G.players[bot].pers;
    if (pe.armed == toward || pe.target == toward) return;  // locked while a betrayal is armed or under way
    if (amount < 0 && pe.quirk == int8_t(Quirk::GrudgeKeeper) && pe.attacks[toward] > 0) return;  // never forgives
    pe.bf[toward] = std::max(0.f, std::min(100.f, pe.bf[toward] + amount));
}

void Rules::bfEvent(int bot, int toward, float weight) {
    if (!isBot(G, bot)) return;
    const Personality& pe = G.players[bot].pers;
    float gain = kArchetypes[pe.arch].bGain * weight;
    if (pe.evo == int8_t(Evo::Vengeful) && weight >= 5.f) gain *= 1.5f;
    bfChange(bot, toward, gain);
}

void Rules::bfEveryone(int toward, float weight) {
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != toward && !G.players[q].eliminated) bfEvent(q, toward, weight);
}

// ------------------------------------------------------------------ turns and rounds
namespace {
int mostResented(const Game& g, int bot) {
    const Personality& pe = g.players[bot].pers;
    int best = -1;
    float bv = -1.f;
    for (int q = 0; q < g.numPlayers && q < 8; ++q) {
        if (q == bot || g.players[q].eliminated) continue;
        // ties: the points leader, then a neighbour
        float v = pe.bf[q] + g.players[q].vp * 0.01f + (areNeighbours(g, bot, q) ? 0.005f : 0.f);
        if (v > bv) {
            bv = v;
            best = q;
        }
    }
    return best;
}
}  // namespace

void Rules::persTurnStart(int p) {
    for (int q = 0; q < G.numPlayers; ++q) G.players[q].pers.nearTurn &= uint16_t(~(1u << p));
    if (!isBot(G, p) || G.players[p].eliminated) return;
    Player& pl = G.players[p];
    Personality& pe = pl.pers;
    // Grey Fanatics (Yin): a betrayal armed at the start of round 2
    if (fquirkIs(G, p, "yin", 0) && G.round >= 2 && !(pe.flags & 1)) {
        pe.flags |= 1;
        if (pe.target < 0 && pe.armed < 0) pe.armed = int8_t(mostResented(G, p));
    }
    if (pe.target >= 0) return;
    if (pe.armed >= 0) {
        // each turn: a B-Factor% chance that the armed betrayal becomes an objective
        if (G.players[pe.armed].eliminated) {
            pe.armed = -1;
        } else if (int(prand(G, 10, p, pl.turns, 0, 100)) + 1 <= int(pe.bf[pe.armed])) {
            giveBetrayal(p, pe.armed);
        }
        return;
    }
    if (pl.turns > 0 && pl.turns % 8 == 0) {  // after every 8 of its own turns: the d100
        int t = mostResented(G, p);
        if (t >= 0 && int(prand(G, 11, p, pl.turns, 0, 100)) + 1 <= int(pe.bf[t])) {
            pe.armed = int8_t(t);
            if (debugBetrayal()) std::fprintf(stderr, "BETRAY r%d %s arms against %s (B %.1f)\n", G.round, pn(p).c_str(), pn(t).c_str(), pe.bf[t]);
        }
    }
}

void Rules::giveBetrayal(int bot, int t) {
    Personality& pe = G.players[bot].pers;
    pe.armed = -1;
    const Player& tp = G.players[t];
    // what is possible against them now, and its weight for this archetype
    struct C {
        Betrayal k;
        int arg;
        int w;
    };
    std::vector<C> cands;
    const int a = pe.now >= 0 ? pe.now : pe.arch;
    static const int8_t kW[int(Arch::Count)][int(Betrayal::Count)] = {
        // BreakP TradeN Threat Attack Embargo Vote Refuse Take Loan Rumor Kingslay (never rolled)
        {2, 2, 1, 1, 1, 4, 2, 1, 1, 4, 0},  // Politician
        {1, 1, 1, 1, 1, 3, 4, 1, 1, 3, 0},  // Peacekeeper
        {1, 1, 4, 4, 2, 1, 1, 4, 1, 1, 0},  // Warlord
        {4, 2, 2, 3, 2, 1, 1, 3, 2, 2, 0},  // Betrayer
        {3, 3, 2, 1, 1, 1, 1, 1, 4, 3, 0},  // Swindler
        {1, 4, 4, 1, 2, 1, 1, 1, 3, 1, 0},  // Merchant
        {2, 1, 3, 2, 2, 1, 1, 4, 1, 2, 0},  // Opportunist
        {1, 1, 1, 3, 2, 1, 1, 4, 1, 1, 0},  // Rusher
        {1, 1, 1, 1, 3, 2, 4, 1, 1, 2, 0},  // Turtler
        {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0},  // Chaotic (not used: it plays as its pick)
    };
    auto add = [&](Betrayal k, int arg) { cands.push_back({k, arg, kW[a][int(k)]}); };
    for (const Promise& pr : G.promises)
        if (pr.state == kPromiseActive && pr.by == bot && pr.to == t) {
            add(Betrayal::BreakPromise, -1);
            break;
        }
    for (int16_t n : G.players[bot].notes)
        if (noteOwner(n) == t) {
            bool third = false;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != bot && q != t && !G.players[q].eliminated) third = true;
            if (third) add(Betrayal::TradeAwayNote, n);
            break;
        }
    if (tp.tradeGoods + tp.commodities >= 1 && !threatSystems(G, bot, t).empty()) add(Betrayal::Threaten, -1);
    // their systems our ships can reach this turn (any ship that can get there)
    auto reachable = [&](int s) {
        for (size_t from = 0; from < G.systems.size(); ++from) {
            const Force& f = G.systems[from].space[bot];
            if (!f.nonFighterShips()) continue;
            for (int ut : kShipIdx)
                if (f.n[ut] && ut != int(UnitType::Fighter) && shipReach(G, bot, int(from), s, UnitType(ut)).ok) return true;
        }
        return false;
    };
    int weakest = -1, weakN = 1 << 20, embargo = -1, take = -1;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        bool theirs = hasUnits(G, t, int(s));
        bool planet = false;
        for (int16_t x : G.systems[s].planets)
            if (G.planets[x].owner == t && !isHomePlanet(G, x)) planet = true;
        if (!theirs && !planet) continue;
        if (!reachable(int(s))) continue;
        int n = G.systems[s].space[t].count(UnitType::Fighter) + G.systems[s].space[t].nonFighterShips();
        for (int16_t x : G.systems[s].planets) n += G.planets[x].units[t].groundForces() + G.planets[x].units[t].structures();
        if (theirs && n < weakN) {
            weakN = n;
            weakest = int(s);
        }
        if (planet) {
            if (embargo < 0 && G.systems[s].home != t) embargo = int(s);
            if (take < 0) take = int(s);
        }
    }
    if (weakest >= 0) add(Betrayal::AttackWeakest, weakest);
    if (embargo >= 0) add(Betrayal::Embargo, embargo);
    if (take >= 0) add(Betrayal::TakePlanet, take);
    if (G.custodiansRemoved) add(Betrayal::VoteAgainst, -1);
    add(Betrayal::RefuseOffers, -1);
    if (tp.tradeGoods >= 2 && (areNeighbours(G, bot, t) || isFaction(G, bot, "hacan") || isFaction(G, t, "hacan")))
        add(Betrayal::LoanScam, -1);
    int rumor = -1;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != bot && q != t && isBot(G, q) && !G.players[q].eliminated && (rumor < 0 || areNeighbours(G, q, t))) rumor = q;
    if (rumor >= 0) add(Betrayal::Rumor, rumor);
    int total = 0;
    for (const C& c : cands) total += c.w;
    if (total <= 0) return;
    int r = int(prand(G, 12, bot, t, G.players[bot].turns, uint32_t(total)));
    C pickC = cands[0];
    for (const C& c : cands) {
        if (r < c.w) {
            pickC = c;
            break;
        }
        r -= c.w;
    }
    pe.target = int8_t(t);
    pe.kind = int8_t(pickC.k);
    pe.arg = int16_t(pickC.arg);
    pe.since = int8_t(G.round);
    if (debugBetrayal())
        std::fprintf(stderr, "BETRAY r%d %s gets '%s' against %s (B %.1f)\n", G.round, pn(bot).c_str(), kBetrayals[pe.kind].name,
                     pn(t).c_str(), pe.bf[t]);
    announceBetrayal(bot, t, int(pickC.k), pickC.arg);
    if (pickC.k == Betrayal::Rumor) {  // whispered at once: another bot now trusts them less
        Personality& other = G.players[pickC.arg].pers;
        if (other.armed != t && other.target != t) other.bf[t] = std::min(100.f, other.bf[t] + 5.f);
        betrayalDone(bot, true);
    }
}

// The owner asked for a banner that waits for A whenever a betrayal begins
// (RULES-CHECK 0.12.0 #4): who was offended, what it will do and to whom.
void Rules::announceBetrayal(int bot, int t, int kind, int arg) {
    const Betrayal k = Betrayal(kind);
    const std::string b = pn(bot), them = pn(t);
    auto sys = [&](int s) { return s >= 0 && s < int(G.systems.size()) ? sysName(s) : std::string("their space"); };
    std::string what;
    switch (k) {
        case Betrayal::BreakPromise: what = b + " will break its word to " + them + "."; break;
        case Betrayal::TradeAwayNote: what = b + " will sell " + them + "'s promissory note to a rival."; break;
        case Betrayal::Threaten: what = b + " will demand tribute from " + them + " - pay, or face its guns."; break;
        case Betrayal::AttackWeakest: what = b + " will strike " + them + " where they are weakest: " + sys(arg) + "."; break;
        case Betrayal::Embargo: what = b + "'s warships will blockade " + them + "'s world in " + sys(arg) + "."; break;
        case Betrayal::VoteAgainst: what = b + " will vote against " + them + " at the next agenda."; break;
        case Betrayal::RefuseOffers: what = b + " turns its back on " + them + ": no deals for two rounds."; break;
        case Betrayal::TakePlanet: {
            std::string planet;
            if (arg >= 0 && arg < int(G.systems.size()))
                for (int16_t x : G.systems[arg].planets)
                    if (G.planets[x].owner == t && planet.empty()) planet = kPlanets[G.planets[x].def].name;
            what = planet.empty() ? b + " will seize one of " + them + "'s planets." : b + " will seize " + planet + " from " + them + ".";
            break;
        }
        case Betrayal::LoanScam: what = b + " will borrow from " + them + " - and never pay it back."; break;
        case Betrayal::Rumor:
            what = b + " whispers lies about " + them + (arg >= 0 && arg < G.numPlayers ? " to " + pn(arg) : std::string()) + ".";
            break;
        case Betrayal::Kingslay: what = b + " will take Mecatol Rex from " + them + "."; break;
        default: what = b + " plots against " + them + "."; break;
    }
    Event e;
    e.kind = EventKind::Betrayal;
    e.player = int8_t(bot);
    e.other = int8_t(t);
    e.card = int16_t(k);
    e.label = kBetrayals[int(k)].name;
    e.system = int16_t(k == Betrayal::AttackWeakest || k == Betrayal::Embargo || k == Betrayal::TakePlanet || k == Betrayal::Kingslay ? arg
                                                                                                                                  : -1);
    e.count = int16_t(k == Betrayal::Rumor ? arg : -1);
    e.text = k == Betrayal::Kingslay ? "Kingslay! " + them + " has held Mecatol Rex for too long. " + what
                                     : b + " was offended by " + them + "! " + what;
    E.emit(e);
}

void Rules::betrayalDone(int bot, bool completed) {
    Personality& pe = G.players[bot].pers;
    if (pe.target < 0) return;
    if (debugBetrayal())
        std::fprintf(stderr, "BETRAY r%d %s %s '%s' against %s\n", G.round, pn(bot).c_str(), completed ? "completes" : "gives up",
                     kBetrayals[pe.kind].name, pn(pe.target).c_str());
    const int t = pe.target;
    pe.target = -1;
    pe.kind = -1;
    pe.arg = -1;
    if (completed) ++pe.betrayals;
    pe.bf[t] = kArchetypes[pe.arch].bStart;  // reset, and the B-Factor moves again
}

// ------------------------------------------------------------------ Kingslay (0.19.0)
// The owner: "In current games where one person is on Mecatol, no one has the
// urgency to kick off that player." Once a player has held Mecatol Rex for
// more than 5 of their own turns, the computer player with the most units on
// the board (not the holder; ties: one of them) gets the betrayal objective
// "Kingslay": take Mecatol Rex from them. It replaces any other betrayal it had,
// ends when it takes the planet (or someone else does), or after 3 rounds.
void Rules::kingslayEndOfTurn(int p) {
    const int mec = G.mecatol();
    if (mec < 0) return;
    if (p >= 0 && p < G.numPlayers) {  // (0.54.0, habits: the turns each player ends holding Mecatol Rex)
        bool held = false;
        for (int16_t x : G.systems[size_t(mec)].planets) held = held || G.planets[size_t(x)].owner == p;
        if (held) ++G.players[size_t(p)].mecHeld;
    }
    if (!(G.extras & 1)) return;
    int holder = -1;
    for (int16_t x : G.systems[mec].planets)
        if (G.planets[x].owner >= 0) holder = G.planets[x].owner;
    if (holder != G.mecHolder) {
        G.mecHolder = int8_t(holder);
        G.mecTurns = 0;
    }
    if (holder < 0 || p != holder) return;
    if (++G.mecTurns <= 5) return;
    for (int q = 0; q < G.numPlayers; ++q)  // one Kingslay at a time against them
        if (isBot(G, q) && G.players[q].pers.target == holder && G.players[q].pers.kind == int8_t(Betrayal::Kingslay)) return;
    auto unitsOf = [&](int q) {
        int n = 0;
        for (const SystemState& s : G.systems) n += s.space[q].ships();
        for (const PlanetState& pl : G.planets) n += pl.units[q].groundForces() + pl.units[q].structures();
        return n;
    };
    std::vector<int> best;
    int most = -1;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == holder || !isBot(G, q) || G.players[q].eliminated) continue;
        const int n = unitsOf(q);
        if (n > most) {
            most = n;
            best.clear();
        }
        if (n == most) best.push_back(q);
    }
    if (best.empty()) return;
    const int bot = best[prand(G, 60, uint32_t(G.round), uint32_t(G.mecTurns), uint32_t(holder), uint32_t(best.size()))];
    Personality& pe = G.players[bot].pers;
    if (pe.target >= 0) betrayalDone(bot, false);  // Kingslay comes first
    pe.armed = -1;
    pe.target = int8_t(holder);
    pe.kind = int8_t(Betrayal::Kingslay);
    pe.arg = int16_t(mec);
    pe.since = int8_t(G.round);
    G.mecTurns = 0;
    if (debugBetrayal())
        std::fprintf(stderr, "BETRAY r%d %s gets 'Kingslay' against %s (%d units)\n", G.round, pn(bot).c_str(), pn(holder).c_str(), most);
    announceBetrayal(bot, holder, int(Betrayal::Kingslay), mec);
}

void Rules::persEndOfTurn(int p) {
    if (!isBot(G, p)) return;
    Personality& pe = G.players[p].pers;
    if (pe.target < 0 || pe.kind != int8_t(Betrayal::Embargo)) return;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (!hasShips(G, p, int(s))) continue;
        for (int16_t x : G.systems[s].planets)
            if (G.planets[x].owner == pe.target) {
                betrayalDone(p, true);
                return;
            }
    }
}

void Rules::persRoundEnd() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!isBot(G, p) || G.players[p].eliminated) continue;
        Personality& pe = G.players[p].pers;
        if (pe.target >= 0) {
            if (G.players[pe.target].eliminated) betrayalDone(p, false);
            else if (pe.kind == int8_t(Betrayal::RefuseOffers) && G.round >= pe.since + 1) betrayalDone(p, true);  // 2 rounds
            else if (G.round >= pe.since + 2) betrayalDone(p, false);  // 3 rounds without managing it: drop it
        }
        if (pe.arch == int8_t(Arch::Chaotic))  // a new archetype for the next round
            pe.now = int8_t(prand(G, 6, p, uint32_t(G.round), 0, uint32_t(Arch::Chaotic)));
    }
    if (G.round == 3 && !G.archRevealed) {
        G.archRevealed = true;
        std::string list;
        for (int p = 0; p < G.numPlayers; ++p)
            if (isBot(G, p)) list += (list.empty() ? "" : ", ") + pn(p) + ": " + archName(G, p);
        if (!list.empty()) {
            Event e;
            e.kind = EventKind::Reveal;
            e.text = "Our Enemies' Ambitions have been Revealed! " + list + ".";
            E.emit(e);
        }
    }
}

// ------------------------------------------------------------------ events
void Rules::persActivate(int p, int sys) {
    if (sys >= 0 && sys == G.nexus) bfEveryone(p, 1.f);  // the Wormhole Nexus
}

void Rules::persMoveNear(int p, int sys) {
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !isBot(G, q) || G.players[q].eliminated) continue;
        Personality& pe = G.players[q].pers;
        if ((pe.nearTurn >> p) & 1) continue;
        bool near = G.systems[sys].home == q;
        for (int16_t x : G.systems[sys].planets)
            if (G.planets[x].owner == q) near = true;
        const int home = G.players[q].homeSystem;
        if (!near && home >= 0)
            for (int16_t a : G.systems[sys].adj)
                if (a == home) near = true;
        if (!near) continue;
        pe.nearTurn |= uint16_t(1u << p);
        bfEvent(q, p, 2.f);
    }
}

void Rules::persCombatStart(int att, int def, int sys, int planet) {
    if (att >= G.numPlayers || def >= G.numPlayers) return;  // (Thunder's Edge: a fight with neutral units isn't personal)
    if (planet < 0) {
        persShips_[0] = G.systems[sys].space[att].nonFighterShips();
        persShips_[1] = G.systems[sys].space[def].nonFighterShips();
    }
    // one grudge per attack, even with a space and a ground combat in the same activation
    const int key = G.turnCount * 64 + att * 8 + def;
    if (key == persCombatKey_) return;
    persCombatKey_ = key;
    if (isBot(G, def)) {
        Personality& pe = G.players[def].pers;
        pe.attacks[att] = int8_t(std::min(100, pe.attacks[att] + 1));
        bfEvent(def, att, 5.f);
    }
    if (isBot(G, att)) {
        Personality& pe = G.players[att].pers;
        if (pe.target == def && (pe.kind == int8_t(Betrayal::AttackWeakest) || pe.kind == int8_t(Betrayal::BreakPromise)))
            betrayalDone(att, true);
    }
}

void Rules::persSpaceCombatEnd(int att, int def, int sys) {
    if (att >= G.numPlayers || def >= G.numPlayers) return;  // (neutral units)
    const int side[2] = {att, def};
    for (int i = 0; i < 2; ++i) {
        int lost = persShips_[i] - G.systems[sys].space[side[i]].nonFighterShips();
        if (lost < 3) continue;
        for (int q = 0; q < G.numPlayers; ++q)
            if (isBot(G, q)) G.players[q].pers.lostBig[side[i]] = int8_t(G.round);
    }
    persShips_[0] = persShips_[1] = 0;
}

void Rules::persControl(int planet, int old, int p) {
    if (G.planets[planet].system == G.mecatol()) {
        bfEveryone(p, 1.f);  // Mecatol Rex taken
        // Kingslay: done when the bot takes it; over when the old holder loses it to anyone else
        for (int q = 0; q < G.numPlayers; ++q) {
            if (!isBot(G, q) || G.players[q].pers.kind != int8_t(Betrayal::Kingslay) || G.players[q].pers.target != old || old == p) continue;
            betrayalDone(q, q == p);
        }
    }
    if (old < 0 || old == p) return;
    bfEvent(old, p, 3.f);  // a planet taken from a bot (with or without a fight)
    if (isBot(G, p)) {
        Personality& pe = G.players[p].pers;
        if (pe.target == old && pe.kind == int8_t(Betrayal::TakePlanet)) betrayalDone(p, true);
    }
}

void Rules::persDealRefused(const Deal& d) {
    if (isBot(G, d.from)) G.players[d.from].pers.refused[d.to] = int8_t(std::min(100, G.players[d.from].pers.refused[d.to] + 1));
    bfEvent(d.from, d.to, 1.f);
    bfEvent(d.to, d.from, 1.f);
}

void Rules::persDealMade(const Deal& d) {
    const float give = sideWorth(G, d.give), get = sideWorth(G, d.get);
    for (int side = 0; side < 2; ++side) {
        const int b = side ? d.to : d.from, o = side ? d.from : d.to;
        if (!isBot(G, b)) continue;
        Personality& pe = G.players[b].pers;
        pe.deals[o] = int8_t(std::min(100, pe.deals[o] + 1));
        const float received = side ? give : get, paid = side ? get : give;
        if (!d.contract()) {
            bfChange(b, o, -1.f);
        } else if (received >= paid + 2.f) {  // a gift: -1 per trade good's worth
            bfChange(b, o, -(received - paid));
            pe.gifts[o] = int8_t(std::min(100, pe.gifts[o] + int(received - paid)));
        } else {
            bfChange(b, o, -2.f);  // a fair deal
        }
    }
    // Pass their note on (betrayal): we handed a target's note to someone else
    for (int side = 0; side < 2; ++side) {
        const int b = side ? d.to : d.from, o = side ? d.from : d.to;
        const int note = side ? d.get.note : d.give.note;
        if (!isBot(G, b) || note < 0) continue;
        Personality& pe = G.players[b].pers;
        if (pe.target >= 0 && pe.kind == int8_t(Betrayal::TradeAwayNote) && noteOwner(note) == pe.target && o != pe.target)
            betrayalDone(b, true);
    }
}

void Rules::persOffered(const Deal& d) {
    if (!isBot(G, d.from)) return;
    Personality& pe = G.players[d.from].pers;
    if (pe.target == d.to && pe.kind == int8_t(Betrayal::Threaten) && d.threat >= 0) betrayalDone(d.from, true);
}

void Rules::persPromise(const Promise& p, bool kept) {
    if (p.by < 0 || p.to < 0) return;
    if (isBot(G, p.to)) {
        Personality& pe = G.players[p.to].pers;
        if (kept) {
            pe.kept[p.by] = int8_t(std::min(100, pe.kept[p.by] + 1));
            bfChange(p.to, p.by, -3.f);
        } else {
            pe.broke[p.by] = int8_t(std::min(100, pe.broke[p.by] + 1));
            bfEvent(p.to, p.by, 10.f);
        }
    }
    if (!kept && isBot(G, p.by)) {
        Personality& pe = G.players[p.by].pers;
        if (pe.target == p.to && (pe.kind == int8_t(Betrayal::BreakPromise) || pe.kind == int8_t(Betrayal::LoanScam)))
            betrayalDone(p.by, true);
    }
}

void Rules::persLoanUnpaid(int by, int to) { bfChange(to, by, 10.f); }

void Rules::persVotes() {
    for (int b = 0; b < G.numPlayers; ++b) {
        if (!isBot(G, b)) continue;
        Personality& pe = G.players[b].pers;
        if (pe.target < 0 || pe.kind != int8_t(Betrayal::VoteAgainst)) continue;
        if (b >= int(G.votedFor.size()) || G.votedFor[b] == kNoVote) continue;
        const int t = pe.target;
        if (t >= int(G.votedFor.size()) || G.votedFor[t] == kNoVote || G.votedFor[t] != G.votedFor[b]) betrayalDone(b, true);
    }
}

}  // namespace ti
