#include "engine/deal.h"

#include <algorithm>
#include <cstring>

#include "engine/query.h"

namespace ti {

namespace {
void putSide(std::vector<int>& v, const DealSide& s) {
    v.push_back(s.tg);
    v.push_back(s.comm);
    v.push_back(s.note);
    v.push_back(s.planet);
    for (const std::vector<int>* list : {&s.fragments, &s.actionCards, &s.captured}) {
        v.push_back(int(list->size()));
        v.insert(v.end(), list->begin(), list->end());
    }
}
void putSide2(std::vector<int>& v, const DealSide& s) {  // (0.34.0: version 2 deals - relics and secrets)
    for (const std::vector<int>* list : {&s.relics, &s.secrets}) {
        v.push_back(int(list->size()));
        v.insert(v.end(), list->begin(), list->end());
    }
}
bool getSide2(const std::vector<int>& v, size_t& i, DealSide& s) {
    for (std::vector<int>* list : {&s.relics, &s.secrets}) {
        if (i >= v.size()) return false;
        int n = v[i++];
        if (n < 0 || n > 40 || i + size_t(n) > v.size()) return false;
        list->assign(v.begin() + long(i), v.begin() + long(i) + n);
        i += size_t(n);
    }
    return true;
}
bool getSide(const std::vector<int>& v, size_t& i, DealSide& s) {
    if (i + 4 > v.size()) return false;
    s.tg = v[i++];
    s.comm = v[i++];
    s.note = v[i++];
    s.planet = v[i++];
    for (std::vector<int>* list : {&s.fragments, &s.actionCards, &s.captured}) {
        if (i >= v.size()) return false;
        int n = v[i++];
        if (n < 0 || n > 40 || i + size_t(n) > v.size()) return false;
        list->assign(v.begin() + long(i), v.begin() + long(i) + n);
        i += size_t(n);
    }
    return true;
}
std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
}  // namespace

std::vector<int> encodeDeal(const Deal& d) {
    // version 2 (Thunder's Edge) only when relics or secrets change hands, version 3 when technologies are shared
    // (Age of Commerce), version 4 when control tokens go on the Vaden's sheet (Discordant Stars): older deals encode
    // as before
    const bool v6 = d.give.hire || d.get.hire;                                   // version 6: Hired Guns (0.43.0)
    const bool v5 = v6 || !d.give.orders.empty() || !d.get.orders.empty();  // version 5: Axis Order cards (Discordant Stars)
    const bool v4 = v5 || d.give.debt || d.get.debt;
    const bool v3 = v4 || !d.give.techs.empty() || !d.get.techs.empty();
    const bool v2 = v3 || !d.give.relics.empty() || !d.give.secrets.empty() || !d.get.relics.empty() || !d.get.secrets.empty();
    std::vector<int> v = {v6 ? 6 : v5 ? 5 : v4 ? 4 : v3 ? 3 : v2 ? 2 : 1, d.from, d.to, d.threat};
    putSide(v, d.give);
    putSide(v, d.get);
    if (v2) {
        putSide2(v, d.give);
        putSide2(v, d.get);
    }
    if (v3)
        for (const DealSide* s : {&d.give, &d.get}) {
            v.push_back(int(s->techs.size()));
            v.insert(v.end(), s->techs.begin(), s->techs.end());
        }
    if (v4) v.insert(v.end(), {d.give.debt, d.get.debt});
    if (v5)
        for (const DealSide* s : {&d.give, &d.get}) {
            v.push_back(int(s->orders.size()));
            v.insert(v.end(), s->orders.begin(), s->orders.end());
        }
    if (v6) v.insert(v.end(), {d.give.hire, d.get.hire});
    v.push_back(int(d.promises.size()));
    for (const Promise& p : d.promises) v.insert(v.end(), {int(p.kind), p.by, p.target, p.amount, p.rounds, p.agenda});
    return v;
}

bool decodeDeal(const std::vector<int>& v, Deal& d) {
    d = Deal();
    if (v.size() < 4 || v[0] < 1 || v[0] > 6) return false;
    size_t i = 1;
    d.from = v[i++];
    d.to = v[i++];
    d.threat = v[i++];
    if (!getSide(v, i, d.give) || !getSide(v, i, d.get)) return false;
    if (v[0] >= 2 && (!getSide2(v, i, d.give) || !getSide2(v, i, d.get))) return false;
    if (v[0] >= 3)
        for (DealSide* s : {&d.give, &d.get}) {
            if (i >= v.size()) return false;
            const int n = v[i++];
            if (n < 0 || n > 8 || i + size_t(n) > v.size()) return false;
            s->techs.assign(v.begin() + long(i), v.begin() + long(i) + n);
            i += size_t(n);
        }
    if (v[0] >= 4) {
        if (i + 2 > v.size()) return false;
        d.give.debt = v[i++];
        d.get.debt = v[i++];
    }
    if (v[0] >= 5)
        for (DealSide* s : {&d.give, &d.get}) {
            if (i >= v.size()) return false;
            const int n = v[i++];
            if (n < 0 || n > 8 || i + size_t(n) > v.size()) return false;
            s->orders.assign(v.begin() + long(i), v.begin() + long(i) + n);
            i += size_t(n);
        }
    if (v[0] >= 6) {
        if (i + 2 > v.size()) return false;
        d.give.hire = v[i++];
        d.get.hire = v[i++];
        if (d.give.hire < 0 || d.give.hire > 3 || d.get.hire < 0 || d.get.hire > 3) return false;
    }
    if (i >= v.size()) return false;
    int n = v[i++];
    if (n < 0 || n > 8 || i + size_t(n) * 6 > v.size()) return false;
    for (int k = 0; k < n; ++k) {
        Promise p;
        int kind = v[i++];
        if (kind <= 0 || kind >= int(PromiseKind::Count)) return false;
        p.kind = PromiseKind(kind);
        p.by = int8_t(v[i++]);
        p.target = int16_t(v[i++]);
        p.amount = int8_t(v[i++]);
        p.rounds = int8_t(v[i++]);
        p.agenda = int16_t(v[i++]);
        d.promises.push_back(p);
    }
    return i == v.size();
}

std::string sideText(const Game& g, const DealSide& s) {
    std::vector<std::string> parts;
    if (s.tg) parts.push_back(plural(s.tg, "trade good", "trade goods"));
    if (s.comm) parts.push_back(plural(s.comm, "commodity", "commodities"));
    if (s.note >= 0) parts.push_back(noteNameOf(g, s.note));
    for (int f : s.fragments)
        if (f >= 0 && f < kExploreCardCount) parts.push_back(kExploreCards[f].name);
    if (!s.actionCards.empty()) parts.push_back(plural(int(s.actionCards.size()), "action card", "action cards"));
    for (int r : s.relics)
        if (r >= 0 && r < kRelicCount) parts.push_back(kRelics[r].name);
    if (!s.secrets.empty()) parts.push_back(plural(int(s.secrets.size()), "secret objective", "secret objectives"));
    for (int t : s.techs)
        if (t >= 0 && t < kTechCount) parts.push_back(std::string(kTechs[t].name) + " (shared)");
    if (s.planet >= 0 && s.planet < int(g.planets.size())) parts.push_back("the planet " + std::string(kPlanets[g.planets[s.planet].def].name));
    if (!s.captured.empty()) parts.push_back(plural(int(s.captured.size()), "captured unit back", "captured units back"));
    if (s.debt) parts.push_back(plural(s.debt, "control token on the Vaden's sheet", "control tokens on the Vaden's sheet"));
    for (int o : s.orders) parts.push_back(axisOrderName(o));
    if (s.hire) parts.push_back("up to " + plural(s.hire, "hired ship", "hired ships") + " (Hired Guns)");
    if (parts.empty()) return "nothing";
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? (i + 1 == parts.size() ? " and " : ", ") : "") + parts[i];
    return out;
}

std::string promiseKindName(PromiseKind k) {
    switch (k) {
        case PromiseKind::AvoidSystem: return "Stay out of a system";
        case PromiseKind::LeaveSystem: return "Leave a system";
        case PromiseKind::AttackSystem: return "Attack a system";
        case PromiseKind::LetPass: return "Let ships stay in a system";
        case PromiseKind::VoteFor: return "Vote for an outcome";
        case PromiseKind::Repay: return "Pay back a loan";
        case PromiseKind::UseAgent: return "Use the agent for the other player";
        case PromiseKind::NoAttack: return "No attacks";
        case PromiseKind::Contract: return "Contractual Obligations: build a ship";
        default: return "";
    }
}

std::string promiseText(const Game& g, const Promise& p, bool withWho) {
    auto sysName = [&](int s) -> std::string {
        if (s < 0 || s >= int(g.systems.size())) return "?";
        const SystemState& sy = g.systems[s];
        if (sy.home >= 0) return playerName(g, sy.home) + "'s home system";
        return kSystems[sy.def].name;
    };
    std::string who = withWho ? playerName(g, p.by) + " " : "";
    std::string other = p.to >= 0 ? playerName(g, p.to) : "the other player";
    std::string rounds = p.untilRound > 0 ? " until the end of round " + std::to_string(p.untilRound)
                                          : " for " + plural(std::max<int>(1, p.rounds), "round", "rounds");
    switch (p.kind) {
        case PromiseKind::AvoidSystem: return who + "stays out of " + sysName(p.target) + rounds;
        case PromiseKind::LeaveSystem: return who + "takes its ships out of " + sysName(p.target) + " by the end of its next turn";
        case PromiseKind::AttackSystem: return who + "attacks in " + sysName(p.target) + rounds;
        case PromiseKind::LetPass: return who + "lets " + other + "'s units stay in " + sysName(p.target) + rounds;
        case PromiseKind::VoteFor: {
            std::string o = "outcome " + std::to_string(p.target);
            for (const auto& oc : voteOutcomes(g, p.agenda))
                if (oc.first == p.target) o = oc.second;
            if (p.agenda >= 0 && p.agenda < kAgendaCount) return who + "votes for " + o + " on " + kAgendas[p.agenda].name;
            return who + "votes for " + o;
        }
        case PromiseKind::Repay:
            return who + "pays back " + plural(p.amount, "trade good", "trade goods") +
                   (p.untilRound > 0 ? " by the end of round " + std::to_string(p.untilRound)
                                     : " within " + plural(std::max<int>(1, p.rounds), "round", "rounds"));
        case PromiseKind::UseAgent: return who + "uses its agent for " + other + " at the next chance" + rounds;
        case PromiseKind::NoAttack: return who + "doesn't attack " + other + rounds;
        case PromiseKind::Contract:
            return who + "picks " + other + " for Contractual Obligations at the next agenda phase (" + other + " produces 1 ship)";
        default: return "";
    }
}

std::string dealText(const Game& g, const Deal& d) {
    std::string out;
    if (!d.give.empty()) out += playerName(g, d.from) + " gives " + sideText(g, d.give) + ". ";
    if (!d.get.empty()) out += playerName(g, d.to) + " gives " + sideText(g, d.get) + ". ";
    for (const Promise& p : d.promises) {
        Promise q = p;
        q.to = int8_t(p.by == d.from ? d.to : d.from);
        out += "Promise: " + promiseText(g, q) + ". ";
    }
    if (d.threat >= 0 && d.threat < int(g.systems.size()))
        out += "If not, " + playerName(g, d.from) + " attacks " + std::string(kSystems[g.systems[d.threat].def].name) + ". ";
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? "Nothing." : out;
}

bool blackMarketPair(const Game& g, int a, int b) {
    return a >= 0 && b >= 0 && ((g.blackMarket[0] == a && g.blackMarket[1] == b) || (g.blackMarket[0] == b && g.blackMarket[1] == a));
}

bool actionCardsTradeable(const Game& g, int a, int b) {
    return isFaction(g, a, "hacan") || isFaction(g, b, "hacan");  // Arbiters
}

bool transactionAllowed(const Game& g, int a, int b) {
    if (a < 0 || b < 0 || a == b || a >= g.numPlayers || b >= g.numPlayers) return false;
    if (g.players[a].eliminated || g.players[b].eliminated) return false;
    if (g.transactions[a][b] + g.transactions[b][a] > 0) return false;  // one per pair per turn / agenda
    if ((g.tradeAnyway >> (a * 8 + b)) & 1) return true;               // Temporal Command Suite
    if (g.phase == Phase::Agenda) return !eventOn(g, kEvHidden);       // LRR 94.6: with anyone (Hidden Agenda: no one)
    if (g.phase != Phase::Action) return false;
    if (g.activePlayer != a && g.activePlayer != b) return false;      // LRR 94.1: the active player and a neighbour
    if (areNeighbours(g, a, b)) return true;
    if (eventOn(g, kEvCommerce)) return true;  // Age of Commerce (galactic event): anyone
    if (stationsOf(g, a) > 0 && stationsOf(g, b) > 0) return true;  // Thunder's Edge: space stations trade with each other
    // Guild Ships (Hacan) / Trade Convoys (Hacan note in the play area): anyone
    for (int p : {a, b}) {
        if (isFaction(g, p, "hacan")) return true;
        for (int16_t n : g.players[p].notesInPlay)
            if (noteType(n) == kNoteFaction && isFaction(g, noteOwner(n), "hacan")) return true;
    }
    return false;
}

int offersLeft(const Game& g, int p, int q) {
    if (p < 0 || q < 0 || p >= g.numPlayers || q >= g.numPlayers || p == q) return 0;
    return std::max(0, 2 - g.offers[p][q]);
}
bool canOffer(const Game& g, int p, int q) {
    return offersLeft(g, p, q) > 0 && !g.players[q].eliminated && !g.players[p].eliminated;
}

bool leaveRequestable(const Game& g, int by, int sys) {
    if (sys < 0 || sys >= int(g.systems.size()) || by < 0) return false;
    if (sys != g.mecatol() && sys != g.nexus) return false;
    return hasShips(g, by, sys) || hasUnits(g, by, sys);
}

int promiseLastRound(const Game& g, const Promise& p) { return g.round + std::max<int>(1, p.rounds) - 1; }

bool noteKnownAway(const Game& g, int note) {
    const int owner = noteOwner(note);
    for (const Player& pl : g.players)
        for (int16_t n : pl.notesInPlay)
            if (n == note) return true;
    int holder = owner;
    for (const auto& dn : g.dealNotes)
        if (dn.first == note) holder = dn.second;
    return holder != owner;
}

// Who is publicly known to hold this note: the last player it went to in a
// deal, else its owner (-1 if it is face up in a play area).
int knownHolder(const Game& g, int note) {
    for (const Player& pl : g.players)
        for (int16_t n : pl.notesInPlay)
            if (n == note) return -1;
    int holder = noteOwner(note);
    for (const auto& dn : g.dealNotes)
        if (dn.first == note) holder = dn.second;
    return holder;
}

std::vector<int> askableNotes(const Game& g, int q) {
    std::vector<int> out;
    if (q < 0 || q >= g.numPlayers) return out;
    auto exists = [&](int n) {
        for (const Player& pl : g.players) {
            for (int16_t x : pl.notes)
                if (x == n) return true;
            for (int16_t x : pl.notesInPlay)
                if (x == n) return true;
        }
        return false;
    };
    // their own notes first (as far as anyone knows, still in their hand) ...
    for (int t = 0; t <= kNoteFaction4; ++t) {
        int n = makeNote(q, t);
        if (exists(n) && knownHolder(g, n) == q) out.push_back(n);
    }
    // ... then other players' notes they received in a deal
    for (const auto& dn : g.dealNotes)
        if (noteOwner(dn.first) != q && knownHolder(g, dn.first) == q && exists(dn.first) &&
            std::find(out.begin(), out.end(), int(dn.first)) == out.end())
            out.push_back(dn.first);
    return out;
}

bool agentLendable(const Game& g, int p) {
    if (p < 0 || p >= g.numPlayers) return false;
    // 0.43.0 (owner, D28 / C3 / C4 / D102): every agent the game can use for another player - the ones whose
    // abilities ask the owner when someone else could gain from them (Rules::agentUser), plus Z'eu, Brother Milor
    // and Cordo Haved
    // (fixes 24: Emissary Taivra too - after the other player activates a system with a non-delta wormhole)
    if (g.fixes >= 24 && isFaction(g, p, "ghost")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    // (fixes 26, the Empyrean review - owner 2026-11-12) Acamar too: "After a player moves ships into ..."
    if (g.fixes >= 26 && isFaction(g, p, "empyrean")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    // (fixes 26, the Nomad review - owner 2026-11-13) Field Marshal Mercer / The Thundarian for another player
    if (g.fixes >= 26 && isFaction(g, p, "nomad")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    // (fixes 26, the Argent review - owner 2026-11-13) Trilossa Aun Mirik: "When a player produces ground forces"
    if (g.fixes >= 26 && isFaction(g, p, "argent")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    // (fixes 27, the Naaz-Rokha review - owner 2026-11-13) Garv and Gunn: "At the end of a player's turn ... allow that player"
    if (g.fixes >= 27 && isFaction(g, p, "naaz")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    // (fixes 27, the Saar review - owner 2026-11-13) Captain Mendosa: "When a player activates a system"
    if (g.fixes >= 27 && isFaction(g, p, "saar")) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    static const char* const kLend[] = {"letnev",   "sol",     "jolnar",   "sardakk", "winnu",     "titans",      "yssaril", "naalu",
                                        "yin",      "keleres", "bastion",  "firmament", "obsidian", "augers",     "celdauri", "cheiran",
                                        "cymiae",   "dihmohn", "florzen",  "freesystems", "ghemina", "ghoti",     "gledge",  "khrask",
                                        "kollecc",  "kortali", "kyro",     "lanefir", "lizho",     "mirveda",     "mortheus", "mykomentori",
                                        "nivyn",    "nokar",   "olradin",  "rohdhna", "tnelis",    "vaden",       "vaylerian", "veldyr",
                                        "zealots",  "zelian"};
    for (const char* k : kLend)
        if (isFaction(g, p, k)) return agentReady(g, p) || leaderIndex(g, p, kAgent) >= 0;
    return false;
}

// 0.43.0 (D102): the lent agent's card, shown beside a "use your agent for me" promise in Negotiations
std::string agentCardText(const Game& g, int p) {
    const int i = leaderIndex(g, p, kAgent);
    if (i < 0) return "";
    const int l = g.players[size_t(p)].leaders[size_t(i)].leader;
    return l < 0 ? "" : std::string(kLeaders[l].name) + " (agent): " + leaderText(l);
}

bool promiseHasRounds(PromiseKind k) {
    return k == PromiseKind::AvoidSystem || k == PromiseKind::AttackSystem || k == PromiseKind::LetPass ||
           k == PromiseKind::NoAttack || k == PromiseKind::Repay || k == PromiseKind::UseAgent;
}
bool promiseHasSystem(PromiseKind k) {
    return k == PromiseKind::AvoidSystem || k == PromiseKind::AttackSystem || k == PromiseKind::LetPass ||
           k == PromiseKind::LeaveSystem;
}

namespace {
bool presentIn(const Game& g, int p, int s) {
    if (p < 0 || s < 0) return false;
    if (hasUnits(g, p, s)) return true;
    for (int16_t x : g.systems[s].planets)
        if (g.planets[x].owner == p) return true;
    return false;
}
void sortByDistance(const Game& g, std::vector<int>& v, int fromSys) {
    if (fromSys < 0) return;
    Hex h = g.systems[fromSys].pos;
    std::stable_sort(v.begin(), v.end(),
                     [&](int a, int b) { return hexDistance(g.systems[a].pos, h) < hexDistance(g.systems[b].pos, h); });
}
}  // namespace

std::vector<int> promiseSystems(const Game& g, PromiseKind k, int by, int to) {
    std::vector<int> out;
    if (by < 0 || to < 0 || by >= g.numPlayers || to >= g.numPlayers) return out;
    const int n = int(g.systems.size());
    const int mec = g.mecatol();
    switch (k) {
        case PromiseKind::AvoidSystem:  // stay out of the other player's systems (and Mecatol Rex)
            for (int s = 0; s < n; ++s)
                if (!g.systems[s].isHyperlane() && (presentIn(g, to, s) || s == mec)) out.push_back(s);
            sortByDistance(g, out, g.players[to].homeSystem);
            break;
        case PromiseKind::LeaveSystem:
            for (int s = 0; s < n; ++s)
                if (leaveRequestable(g, by, s)) out.push_back(s);
            break;
        case PromiseKind::LetPass:  // where the promiser could attack the other player's units
            for (int s = 0; s < n; ++s)
                if (!g.systems[s].isHyperlane() && (presentIn(g, by, s) || hasShips(g, to, s))) out.push_back(s);
            sortByDistance(g, out, g.players[to].homeSystem);
            break;
        case PromiseKind::AttackSystem:  // a third player's units
            for (int s = 0; s < n; ++s) {
                if (g.systems[s].isHyperlane()) continue;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (q != by && q != to && !g.players[q].eliminated && hasUnits(g, q, s)) {
                        out.push_back(s);
                        break;
                    }
            }
            sortByDistance(g, out, g.players[by].homeSystem);
            break;
        default: break;
    }
    return out;
}

std::vector<int> threatSystems(const Game& g, int from, int to) {
    std::vector<int> out;
    if (from < 0 || to < 0 || from >= g.numPlayers || to >= g.numPlayers) return out;
    for (int s = 0; s < int(g.systems.size()); ++s) {
        if (!presentIn(g, to, s)) continue;
        bool near = hasShips(g, from, s);
        for (int a : systemsWithin(g, s, 1))
            if (hasShips(g, from, a)) near = true;
        if (near) out.push_back(s);
    }
    sortByDistance(g, out, g.players[from].homeSystem);
    return out;
}

std::vector<std::pair<int, std::string>> voteOutcomes(const Game& g, int a) {
    std::vector<std::pair<int, std::string>> v;
    if (a < 0 || a >= kAgendaCount) return v;
    if (!std::strcmp(kAgendas[a].key, "covert") && g.covertAgenda >= 0) a = g.covertAgenda;
    switch (kAgendas[a].elect) {
        case Elect::ForAgainst:
            v.push_back({0, "For"});
            v.push_back({1, "Against"});
            break;
        case Elect::Player:
            for (int q = 0; q < g.numPlayers; ++q)
                if (!g.players[q].eliminated) v.push_back({q, playerName(g, q)});
            break;
        case Elect::Planet:
        case Elect::NonHomePlanet: {
            int mec = g.mecatol();
            for (size_t x = 0; x < g.planets.size(); ++x) {
                const PlanetState& ps = g.planets[x];
                if (ps.owner < 0) continue;
                if (kAgendas[a].elect == Elect::NonHomePlanet && (isHomePlanet(g, int(x)) || ps.system == mec)) continue;
                v.push_back({int(x), kPlanets[ps.def].name});
            }
            break;
        }
        case Elect::Law:
            for (const Law& l : g.laws) v.push_back({l.agenda, kAgendas[l.agenda].name});
            break;
        case Elect::ScoredSecret:
            for (int q = 0; q < g.numPlayers; ++q)
                for (int16_t o : g.players[q].scored)
                    if (kObjectives[o].kind == 3) v.push_back({o, kObjectives[o].name});
            break;
        case Elect::StrategyCard:
            for (int c = 0; c < kStrategyCards; ++c) v.push_back({c, std::to_string(c + 1) + " " + strategyName(c)});
            break;
        case Elect::Covert: break;
    }
    return v;
}

// Encrypted Trade Hub (Edyn, 0.44.5 with fixes 9): its owner, a party to the deal, may exhaust it to let 1 relic be
// exchanged (the agenda part isn't implemented - laws don't change hands in this game)
int encryptedHubOf(const Game& g, int a, int b) {
    if (g.fixes < 9 || !(g.modules & kModDiscordant)) return -1;
    const int t = findTech("dsedyny");
    for (int q : {a, b})
        if (q >= 0 && q < g.numPlayers && ownsTechKey(g, q, "dsedyny") && !g.players[size_t(q)].techExhausted(t)) return q;
    return -1;
}

std::string checkDeal(const Game& g, const Deal& d, bool counter) {
    (void)counter;
    const int a = d.from, b = d.to;
    if (a < 0 || b < 0 || a >= g.numPlayers || b >= g.numPlayers || a == b) return "Choose another player.";
    if (g.players[a].eliminated || g.players[b].eliminated) return "That player is out of the game.";
    if (d.empty()) return "The deal is empty.";
    if (d.contract() && !transactionAllowed(g, a, b))
        return g.transactions[a][b] + g.transactions[b][a] > 0 ? "You already traded with them this turn."
                                                                : "You can only trade with neighbours on your turn (promises only).";
    auto checkSide = [&](const DealSide& s, int who) -> std::string {
        const Player& pl = g.players[who];
        if (s.tg < 0 || s.comm < 0) return "Negative amounts.";
        if (s.tg > pl.tradeGoods) return playerName(g, who) + " doesn't have that many trade goods.";
        if (s.comm > pl.commodities) return playerName(g, who) + " doesn't have that many commodities.";
        if (s.note >= 0) {
            // the proposer can ask for one of the other player's own notes without knowing whether it's in hand
            if (who == a) {
                if (std::find(pl.notes.begin(), pl.notes.end(), int16_t(s.note)) == pl.notes.end())
                    return playerName(g, who) + " doesn't hold that promissory note.";
            } else if (knownHolder(g, s.note) != who) {
                return "You can only ask for a note they are known to hold (one of their own, or one they got in a deal).";
            }
            if (noteType(s.note) == kNoteAlliance && isFaction(g, who == a ? b : a, "mahact") && noteOwner(s.note) != (who == a ? b : a))
                return "The Mahact can't receive an Alliance (Hubris).";
        }
        for (int f : s.fragments)
            if (std::count(pl.fragments.begin(), pl.fragments.end(), int16_t(f)) <
                std::count(s.fragments.begin(), s.fragments.end(), f))
                return playerName(g, who) + " doesn't have that relic fragment.";
        for (int r : s.relics) {  // Thunder's Edge: The Silver Flame always; any relic with Black Market Dealings
            if (r < 0 || r >= kRelicCount || !pl.hasRelic(r)) return playerName(g, who) + " doesn't hold that relic.";
            if (std::strcmp(kRelics[r].key, "thesilverflame") && !blackMarketPair(g, a, b)) {
                if (encryptedHubOf(g, a, b) < 0) return "Relics can only be traded with Black Market Dealings (or The Silver Flame).";
                int n = 0;  // Encrypted Trade Hub: one relic
                for (const DealSide* x : {&d.give, &d.get})
                    for (int y : x->relics) n += y >= 0 && y < kRelicCount && std::strcmp(kRelics[y].key, "thesilverflame") ? 1 : 0;
                if (n > 1) return "Encrypted Trade Hub lets only 1 relic change hands.";
            }
        }
        for (int t : s.techs) {  // Age of Commerce: a non-faction technology the giver owns and the receiver doesn't
            const Player& rcv = g.players[size_t(who == a ? b : a)];
            if (!eventOn(g, kEvCommerce)) return "Technologies can only be shared in the Age of Commerce.";
            if (t < 0 || t >= kTechCount || kTechs[t].faction >= 0 || !pl.hasTech(t)) return playerName(g, who) + " can't share that technology.";
            if (rcv.hasTech(t)) return "They already own " + std::string(kTechs[t].name) + ".";
        }
        if (!s.secrets.empty()) {
            if (!blackMarketPair(g, a, b)) return "Secret objectives can only be traded with Black Market Dealings.";
            if (who != a) return "You can only give your own secret objectives.";
            for (int o : s.secrets)
                if (std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(o)) == pl.secrets.end())
                    return playerName(g, who) + " doesn't hold that secret objective.";
        }
        if (!s.actionCards.empty()) {
            if (!actionCardsTradeable(g, a, b) && !blackMarketPair(g, a, b)) return "Action cards can only be traded with the Hacan (Arbiters).";
            for (int c : s.actionCards)
                if (std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)) == pl.actionCards.end())
                    return playerName(g, who) + " doesn't hold that action card.";
        }
        if (s.planet >= 0) {
            if (s.planet >= int(g.planets.size()) || g.planets[s.planet].owner != who || !isFaction(g, who, "hacan") ||
                !g.planets[s.planet].units[who].count(UnitType::Mech))
                return "Only a Hacan planet with the Pride of Kenara (mech) can be traded.";
            if (!mechAbilities(g)) return "Articles of War: the Pride of Kenara has lost its ability.";
        }
        if (s.comm > 0 && (g.modules & kModDiscordant) && isFaction(g, who, "axis"))  // Military Industrial Complex
            return "The Axis can't give commodities in a transaction.";
        for (int o : s.orders)  // Arms Dealers (Discordant Stars): Axis Orders from the giver's play area
            if (o < 0 || o >= 8 || g.dsOrders[o] != who || ((g.dsOrderEx >> o) & 1))
                return playerName(g, who) + " doesn't have that Axis Order ready in their play area.";
        if (s.debt) {  // Binding Debts (Discordant Stars): their own control tokens, onto the Vaden's sheet (at most 6 there)
            const int rcv = who == a ? b : a;
            if (s.debt < 0 || !(g.modules & kModDiscordant) || !isFaction(g, rcv, "vaden"))
                return "Control tokens only go on the Vaden Banking Clans' sheet.";
            if (g.dsDebt[who] + s.debt > 6) return "The Vaden's sheet holds at most 6 of " + playerName(g, who) + "'s tokens.";
        }
        if (!s.captured.empty()) {
            // (0.44.8, fixes 12: anyone holding captured units may give them back - a Nekro with a copied Vortex)
            if (g.fixes < 12 && !isFaction(g, who, "cabal")) return "Only the Vuil'raith give captured units back.";
            for (int c : s.captured)
                if (std::find(pl.captured.begin(), pl.captured.end(), int16_t(c)) == pl.captured.end())
                    return "Those units aren't captured.";
        }
        return "";
    };
    std::string e = checkSide(d.give, a);
    if (!e.empty()) return e;
    e = checkSide(d.get, b);
    if (!e.empty()) return e;
    for (const Promise& p : d.promises) {
        if (p.by != a && p.by != b) return "A promise must be made by one of the two players.";
        if (p.rounds < 1 || p.rounds > 3) return "Promises last 1 to 3 rounds.";
        switch (p.kind) {
            case PromiseKind::AvoidSystem:
            case PromiseKind::AttackSystem:
            case PromiseKind::LetPass:
                if (p.target < 0 || p.target >= int(g.systems.size())) return "Choose a system.";
                break;
            case PromiseKind::LeaveSystem:
                if (!leaveRequestable(g, p.by, p.target)) return "That player has nothing to take out of that system.";
                break;
            case PromiseKind::VoteFor:
                if (g.currentAgenda < 0 || p.agenda != g.currentAgenda) return "Votes can only be promised for the agenda being voted on.";
                {
                    bool ok = false;
                    for (const auto& oc : voteOutcomes(g, p.agenda))
                        if (oc.first == p.target) ok = true;
                    if (!ok) return "Choose an outcome to vote for.";
                }
                break;
            case PromiseKind::Repay:
                if (p.amount < 1 || p.amount > 20) return "A loan pays back 1 to 20 trade goods.";
                break;
            case PromiseKind::UseAgent:
                if (!agentLendable(g, p.by)) return playerName(g, p.by) + "'s agent can't be used for other players.";
                break;
            case PromiseKind::Contract:
                if (!ownsTechKey(g, p.by, "dsrohdy")) return playerName(g, p.by) + " doesn't have Contractual Obligations.";
                break;
            case PromiseKind::NoAttack: break;
            default: return "Unknown promise.";
        }
    }
    return "";
}

bool hostileFlag(const Game& g, int a, int b) {
    if (a < 0 || b < 0 || a >= 8 || b >= 8) return false;
    return ((g.hostile >> (a * 8 + b)) & 1) || ((g.hostile >> (b * 8 + a)) & 1);
}

std::vector<const Promise*> activePromises(const Game& g, int a, int b) {
    std::vector<const Promise*> out;
    for (const Promise& p : g.promises)
        if (p.state == kPromiseActive && ((p.by == a && p.to == b) || (p.by == b && p.to == a))) out.push_back(&p);
    return out;
}

Relation relationOf(const Game& g, int a, int b) {
    if (hostileFlag(g, a, b)) return Relation::Hostile;
    bool contract = false, promise = false;
    for (const Promise* p : activePromises(g, a, b)) (p->contract ? contract : promise) = true;
    for (const auto& dn : g.dealNotes) {
        int owner = noteOwner(dn.first), holder = dn.second;
        if (!((owner == a && holder == b) || (owner == b && holder == a))) continue;
        const Player& h = g.players[holder];
        if (std::find(h.notes.begin(), h.notes.end(), dn.first) != h.notes.end() ||
            std::find(h.notesInPlay.begin(), h.notesInPlay.end(), dn.first) != h.notesInPlay.end())
            contract = true;
    }
    return contract ? Relation::Contract : promise ? Relation::Promise : Relation::None;
}

float noteDefaultValue(const Game& g, int note) {
    const int type = noteType(note), owner = noteOwner(note);
    switch (type) {
        case kNoteTradeAgreement: return 1.f;
        case kNoteCeasefire: return 2.f;
        case kNoteSupportThrone: return 4.f;
        case kNotePoliticalSecret: return 1.f;
        case kNoteAlliance: return 2.f;
        default: break;
    }
    if (owner < 0 || owner >= g.numPlayers) return 1.f;
    static const struct {
        const char* faction;
        int type;
        float v;
    } table[] = {{"muaat", kNoteFaction, 4}, {"jolnar", kNoteFaction, 3}, {"titans", kNoteFaction, 3}, {"sol", kNoteFaction, 2},
                 {"letnev", kNoteFaction, 2}, {"hacan", kNoteFaction, 2}, {"sardakk", kNoteFaction, 2}, {"xxcha", kNoteFaction, 2},
                 {"l1z1x", kNoteFaction, 2}, {"mentak", kNoteFaction, 2}, {"naalu", kNoteFaction, 2}, {"yssaril", kNoteFaction, 2},
                 {"ghost", kNoteFaction, 2}, {"nekro", kNoteFaction, 2}, {"empyrean", kNoteFaction2, 2}, {"mahact", kNoteFaction, 2},
                 {"naaz", kNoteFaction, 2}, {"nomad", kNoteFaction, 2}, {"arborec", kNoteFaction, 1}, {"yin", kNoteFaction, 1},
                 {"saar", kNoteFaction, 1}, {"winnu", kNoteFaction, 1}, {"empyrean", kNoteFaction, 1}, {"argent", kNoteFaction, 1},
                 {"cabal", kNoteFaction, 1}};
    for (const auto& e : table)
        if (e.type == type && !std::strcmp(kFactions[g.players[owner].faction].key, e.faction)) return e.v;
    return 1.f;
}

}  // namespace ti
