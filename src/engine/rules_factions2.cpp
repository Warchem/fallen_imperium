// Stage 5, batch 2: the Arborec, L1Z1X Mindnet, Mentak Coalition, Naalu
// Collective, Yin Brotherhood and Yssaril Tribes. Original Prophecy of Kings
// card versions (owner, RULES-CHECK 0.6.0 #1 / 0.7.0 #1). Agents are only
// offered when their owner is involved (owner OK'd, RULES-CHECK 0.6.0 #2);
// Ssruu (Yssaril agent) counts as any other player's agent.
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

namespace {
constexpr int kShipMask = kShipMaskAll;
constexpr int kNonFighter = kShipMask & ~(1 << int(UnitType::Fighter));
constexpr int kGroundMask = (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech));

void abilityEvent2(Engine& E, int p, const std::string& name, const std::string& text, int sys = -1) {
    Event e;
    e.kind = EventKind::Ability;
    e.player = int8_t(p);
    e.label = name;
    e.text = text;
    e.system = int16_t(sys);
    E.emit(e);
}

void heroEvent2(Engine& E, const Game& G, int p, const std::string& text) {
    int i = leaderIndex(G, p, kHero);
    Event e;
    e.kind = EventKind::Leader;
    e.player = int8_t(p);
    e.card = int16_t(i >= 0 ? G.players[p].leaders[i].leader : 0);
    e.label = "hero";
    e.text = text;
    E.emit(e);
}

int unitCost(const Game& g, int p, UnitType t) { return (unitCost2(g, p, t) + 1) / 2; }

std::string sysLabel2(const Game& g, int s) {
    const SystemState& sy = g.systems[s];
    if (sy.home >= 0 && kSystems[sy.def].back != Back::Home)  // Ghoti Mobile Command
        return std::string(kSystems[sy.def].name) + " (" + playerName(g, sy.home) + " home)";
    if (sy.home >= 0) return playerName(g, sy.home) + " home";
    return kSystems[sy.def].name;
}

// Letani Ospha: every swap of one of p's non-fighter ships for a pricier one (up to 2 more).
std::vector<Option> osphaOptions(const Game& g, int p) {
    std::vector<Option> o;
    for (size_t s = 0; s < g.systems.size(); ++s) {
        const Force& f = g.systems[s].space[p];
        for (int t : kShipIdx) {
            if (!f.n[t] || t == int(UnitType::Fighter)) continue;
            const int c = unitCost2(g, p, UnitType(t));
            for (int u : kShipIdx) {
                if (u == t || u == int(UnitType::Fighter)) continue;
                const int cu = unitCost2(g, p, UnitType(u));
                if (cu <= c || cu > c + 4 || unitsAvailable(g, p, UnitType(u)) <= 0) continue;
                o.push_back({int(s) * 256 + t * 16 + u,
                             sysLabel2(g, int(s)) + ": " + unitTypeName(UnitType(t)) + " -> " + unitTypeName(UnitType(u)),
                             std::to_string((cu + 1) / 2) + " resources instead of " + std::to_string((c + 1) / 2)});
            }
        }
    }
    return o;
}

// (fixes 26, the Arborec review - owner 2026-11-12) Letani Ospha: "choose a player's non-fighter ship: that player
// may replace that ship with one from their reinforcements that costs up to 2 more than the replaced ship" - any
// player's ship, and any other non-fighter ship type that costs at most 2 more (equal or cheaper too)
std::vector<int> osphaReplacements(const Game& g, int q, int t) {
    std::vector<int> v;
    const int c = unitCost2(g, q, UnitType(t));
    for (int u : kShipIdx)
        if (u != t && u != int(UnitType::Fighter) && unitCost2(g, q, UnitType(u)) <= c + 4 && unitsAvailable(g, q, UnitType(u)) > 0) v.push_back(u);
    return v;
}
std::vector<Option> osphaShips(const Game& g) {  // option id = (player * 256 + system) * 16 + ship type
    std::vector<Option> o;
    for (int q = 0; q < g.numPlayers; ++q) {
        if (g.players[size_t(q)].eliminated) continue;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            const Force& f = g.systems[s].space[size_t(q)];
            for (int t : kShipIdx)
                if (f.n[t] && t != int(UnitType::Fighter) && !osphaReplacements(g, q, t).empty())
                    o.push_back({(q * 256 + int(s)) * 16 + t, playerName(g, q) + ": " + unitTypeName(UnitType(t)) + " in " + sysLabel2(g, int(s)), ""});
        }
    }
    return o;
}

bool othersHaveCards(const Game& g, int p) {
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != p && !g.players[q].eliminated && !g.players[q].actionCards.empty()) return true;
    return false;
}

void moveCard(Player& from, Player& to, int card) {
    auto it = std::find(from.actionCards.begin(), from.actionCards.end(), int16_t(card));
    if (it == from.actionCards.end()) return;
    from.actionCards.erase(it);
    to.actionCards.push_back(int16_t(card));
}
}  // namespace

// ------------------------------------------------------------------ shared
void Rules::gainTG(int p, int n) {
    if (n <= 0 || p < 0 || p >= G.numPlayers) return;
    Player& pl = G.players[p];
    pl.tradeGoods = int8_t(std::min(99, pl.tradeGoods + n));
    // Artuno the Betrayer (Nomad agent): the same number of trade goods go on the card - only for trade goods
    // from the supply (0.43.0, fixes 2: not those another player hands over in a deal or a loan)
    // (fixes 26, the Nomad review - owner 2026-11-13: Ssruu has the text of every other agent - the Yssaril may use it as Artuno)
    if (!artunoPay_ && !(tgFromPlayer_ && G.fixes >= 2) && !pl.eliminated && (fac(p, "nomad") || (G.fixes >= 26 && fac(p, "yssaril"))) &&
        canUseNomadAgent(p, 2)) {
        curValue_ = n;
        bool use = fyes(p, Ab::NomadArtuno, "Exhaust Artuno the Betrayer to put " + std::to_string(n) + " trade good" + (n > 1 ? "s" : "") +
                                                " on it too?",
                        leaderText(findLeader("nomadagentartuno")));
        curValue_ = 0;
        if (use) {
            pl.agentTG = int8_t(std::min(99, pl.agentTG + n));
            spendNomadAgent(p, 2);
        }
    }
    if (dealInProgress_) return;  // checked once the deal is done
    pillageCheck(p);
}

void Rules::pillageCheck(int p) {
    Player& pl = G.players[p];
    if (pl.tradeGoods < 3 || pl.eliminated) return;
    // Pillage (Mentak): after a neighbour gains trade goods (or resolves a transaction) and then has 3 or more
    for (int m = 0; m < G.numPlayers; ++m) {
        if (m == p || !fac(m, "mentak") || G.players[m].eliminated || !areNeighbours(G, m, p)) continue;
        bool promised = false;  // Promise of Protection in p's play area
        for (int16_t note : pl.notesInPlay)
            if (noteOwner(note) == m && noteType(note) == kNoteFaction) promised = true;
        if (promised) continue;
        std::vector<Option> opts;
        opts.push_back({0, "Take 1 trade good", pn(p) + " has " + std::to_string(pl.tradeGoods)});
        if (pl.commodities > 0) opts.push_back({1, "Take 1 commodity", pn(p) + " has " + std::to_string(pl.commodities) + " commodities"});
        opts.push_back({-1, "Don't pillage", ""});
        int c = fpick(m, Ab::Pillage, "Pillage " + pn(p) + "?", opts, abilityText(G.players[m].faction, 1));
        if (c >= 0 && (G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtVault(p, m, c == 0 ? "1 trade good" : "1 commodity")) c = -1;
        if (c < 0) continue;
        if (c == 0) pl.tradeGoods = int8_t(pl.tradeGoods - 1);
        else pl.commodities = int8_t(pl.commodities - 1);
        G.players[m].tradeGoods = int8_t(G.players[m].tradeGoods + 1);
        G.players[m].usedFactionAbility = true;
        abilityEvent2(E, m, "Pillage", pn(m) + " pillages 1 " + (c == 0 ? "trade good" : "commodity") + " from " + pn(p) + ".");
        // Suffi An (Mentak agent): after the Mentak player's Pillage
        if (canUseAgent(m, "mentak") && fyes(m, Ab::MentakAgent, "Exhaust Suffi An: you and " + pn(p) + " each draw 1 action card?",
                                             leaderText(findLeader("mentakagent")))) {
            spendAgent(m, "mentak");
            drawActionCards(m, 1, "Suffi An");
            drawActionCards(p, 1, "Suffi An");
        }
        // (fixes 27, the Mentak review - owner 2026-11-13) Ssruu has the text of every other player's agent: after the Pillage
        // is used against another player, the Yssaril may use it as Suffi An - they and that player each draw 1 action card
        if (G.fixes >= 27)
            for (int q : initiativeOrder(G)) {
                if (q == m || q == p || !fac(q, "yssaril") || !canUseAgent(q, "mentak")) continue;
                if (!fyes(q, Ab::MentakAgent, "Exhaust Ssruu as Suffi An: you and " + pn(p) + " each draw 1 action card?",
                          leaderText(findLeader("mentakagent"))))
                    continue;
                spendAgent(q, "mentak");
                drawActionCards(q, 1, "Suffi An");
                drawActionCards(p, 1, "Suffi An");
            }
        break;
    }
}

bool Rules::canUseAgent(int who, const char* factionKey) const {
    if (who < 0 || who >= G.numPlayers || G.players[who].eliminated) return false;
    if (fac(who, factionKey)) return agentReady(G, who);
    // Ssruu (Yssaril agent): has the text of every other player's agent, even an exhausted one
    return fac(who, "yssaril") && agentReady(G, who) && playerOfFaction(G, factionKey) >= 0;
}

void Rules::spendAgent(int who, const char* factionKey) {
    exhaustAgent(who);
    if (!fac(who, factionKey)) say(who, pn(who) + " uses Ssruu as " + pn(playerOfFaction(G, factionKey)) + "'s agent.");
}

// ------------------------------------------------------------------ status phase
void Rules::mitosis() {
    for (int p : initiativeOrder(G)) {
        if (!fac(p, "arborec")) continue;
        const bool inf = unitsAvailable(G, p, UnitType::Infantry) > 0;
        const bool mech = unitsAvailable(G, p, UnitType::Mech) > 0 && mechAbilities(G);  // (Letani Behemoth: Deploy)
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p)) {
            if (isDMZ(G, x) || G.planets[x].destroyed) continue;
            const Force& f = G.planets[x].units[p];
            if (inf) opts.push_back({x, planetName(x), std::to_string(f.groundForces()) + " ground forces there"});
            if (mech && f.count(UnitType::Infantry))  // Letani Behemoth: replace 1 infantry with a mech instead
                opts.push_back({10000 + x, "Letani Behemoth (mech) on " + planetName(x), "Deploy: instead of the infantry."});
        }
        if (opts.empty()) continue;
        int c = opts.size() == 1 ? opts[0].id
                                 : fpick(p, Ab::Mitosis, "Mitosis: place 1 infantry (or deploy a mech) where?", opts,
                                         abilityText(G.players[p].faction, 0));
        const bool deploy = c >= 10000;
        const int x = c % 10000, sys = G.planets[x].system;
        if (deploy) {
            G.planets[x].units[p].remove(UnitType::Infantry, 1, false);
            addUnits(p, UnitType::Mech, 1, sys, x);
        } else {
            addUnits(p, UnitType::Infantry, 1, sys, x);
        }
        G.players[p].usedFactionAbility = true;
        abilityEvent2(E, p, "Mitosis", pn(p) + " grows 1 " + (deploy ? "mech" : "infantry") + " on " + planetName(x) + " (Mitosis).", sys);
    }
}

void Rules::bioplasmosis() {
    for (int p : initiativeOrder(G)) {
        if (!owns(p, "bio")) continue;
        std::vector<int> stuck;  // planets whose infantry have nowhere to go
        // (0.44.6, fixes 10: infantry that arrived by Bioplasmosis stay - each moves once, to the same or a next system)
        std::map<int, int> arrived;
        const bool v10 = G.fixes >= 10;
        int moves = 0;
        for (int guard = 0; guard < (v10 ? 40 : 12); ++guard) {
            std::vector<Option> from;
            for (int x : controlledPlanets(G, p)) {
                int n = G.planets[x].units[p].count(UnitType::Infantry) - (v10 ? arrived[x] : 0);
                if (n <= 0 || std::find(stuck.begin(), stuck.end(), x) != stuck.end()) continue;
                from.push_back({x, planetName(x), std::to_string(n) + " infantry"});
            }
            if (from.empty()) break;
            from.push_back({-1, moves ? "Done" : "Don't move any", ""});
            int x = fpick(p, Ab::Bioplasmosis, "Bioplasmosis: move infantry from which planet?", from, techSummary(findTech("bio")), false,
                          true);
            if (x < 0) break;
            const int sys = G.planets[x].system;
            std::vector<Option> to;
            for (int y : controlledPlanets(G, p)) {
                if (y == x || isDMZ(G, y) || G.planets[y].destroyed) continue;
                int s2 = G.planets[y].system;
                if (s2 == sys || adjacentSystems(G, s2, sys)) to.push_back({y, planetName(y), ""});
            }
            if (to.empty()) {
                stuck.push_back(x);
                continue;
            }
            to.push_back({-1, "Cancel", ""});
            const int prevValue = curValue_;
            curValue_ = x + 1;  // (0.44.6: the AI sees where they come from)
            int y = fpick(p, Ab::Bioplasmosis, "Bioplasmosis: move them to?", to, "", false, true);
            curValue_ = prevValue;
            if (y < 0) {
                stuck.push_back(x);
                continue;
            }
            const int have = G.planets[x].units[p].count(UnitType::Infantry) - (v10 ? arrived[x] : 0);
            int k = 1;
            if (have > 1) {
                std::vector<Option> ks;
                for (int i = 1; i <= have; ++i) ks.push_back({i, std::to_string(i) + " infantry", ""});
                k = fpick(p, Ab::Bioplasmosis, "Bioplasmosis: how many?", ks);
            }
            G.planets[x].units[p].remove(UnitType::Infantry, k, false);
            G.planets[y].units[p].add(UnitType::Infantry, k);
            arrived[y] += k;
            ++moves;
            say(p, pn(p) + " moves " + std::to_string(k) + " infantry from " + planetName(x) + " to " + planetName(y) + " (Bioplasmosis).", sys);
        }
    }
}

void Rules::oracle() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!fac(p, "naalu") || !heroUnlocked(G, p)) continue;
        int notes = 0;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated) notes += int(G.players[q].notes.size());
        if (notes == 0) continue;
        if (!fyes(p, Ab::NaaluHero, "Use The Oracle: every other player gives you a promissory note?", "The hero is purged.")) continue;
        purgeHero(p);
        heroEvent2(E, G, p, pn(p) + " uses The Oracle: every other player gives them a promissory note.");
        for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
            if (q == p || G.players[q].notes.empty()) continue;
            std::vector<Option> opts = notesToGive(q, p);
            if (opts.empty()) continue;
            int n = opts.size() == 1 ? opts[0].id : pick(q, Tag::GiveNote, "The Oracle: give " + pn(p) + " which promissory note?", opts);
            giveNote(q, p, n);
        }
    }
}

int Rules::extraStatusTokens(int p) {
    // Cybernetic Enhancements (L1Z1X's note) - the Omega version (Codex); (fixes 26, the L1Z1X review - owner 2026-11-12)
    // without the Codex the original card (startOfTurn2)
    int L = playerOfFaction(G, "l1z1x");
    if (L < 0 || L == p || !holdsNote(G, p, L, kNoteFaction)) return 0;
    if (G.fixes >= 26 && !codexOn(G)) return 0;
    if (!fyes(p, Ab::CyberneticEnhancements, "Play Cybernetic Enhancements for 1 extra command token?",
              factionNoteText(G.players[L].faction, kNoteFaction)))
        return 0;
    returnNote(p, makeNote(L, kNoteFaction));
    abilityEvent2(E, p, "Cybernetic Enhancements", pn(p) + " plays Cybernetic Enhancements: 1 extra command token.");
    return 1;
}

// ------------------------------------------------------------------ strategy phase
void Rules::telepathic() {
    G.naaluToken = -1;
    const int n = playerOfFaction(G, "naalu");
    if (n < 0) return;
    // Gift of Prescience: its holder takes the "0" token; Naalu can't use Telepathic this round
    for (int q : fromSpeaker(G)) {
        if (q == n || !holdsNote(G, q, n, kNoteFaction)) continue;
        if (!fyes(q, Ab::GiftOfPrescience, "Play Gift of Prescience: you go first this round?",
                  factionNoteText(G.players[n].faction, kNoteFaction)))
            continue;
        auto& h = G.players[q].notes;
        h.erase(std::find(h.begin(), h.end(), int16_t(makeNote(n, kNoteFaction))));
        G.players[q].notesInPlay.push_back(makeNote(n, kNoteFaction));
        G.naaluToken = int8_t(q);
        abilityEvent2(E, q, "Gift of Prescience", pn(q) + " plays Gift of Prescience and takes the Naalu \"0\" token: first this round.");
        return;
    }
    G.naaluToken = int8_t(n);
    G.players[n].usedFactionAbility = true;
    abilityEvent2(E, n, "Telepathic", pn(n) + " takes the Naalu \"0\" token: first in the action phase (Telepathic).");
}

// ------------------------------------------------------------------ tactical action
// Neuroglaive (Naalu): the active player loses a fleet token (0.44.6, fixes 10: before Minister of Peace, Nullification
// Field or Starlancer can end the turn - it comes "after activation" with them, and it isn't optional)
void Rules::neuroglaive(int p, int sys) {
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && owns(q, "ng") && hasShips(G, q, sys) && !G.players[q].eliminated && G.players[p].fleet > 0) {
            G.players[p].fleet = int8_t(G.players[p].fleet - 1);
            abilityEvent2(E, q, "Neuroglaive", pn(p) + " loses a fleet token (Neuroglaive).", sys);
            enforceFleetLimit(p);
        }
}

void Rules::afterActivation2(int p, int sys) {
    if (G.fixes < 10) neuroglaive(p, sys);
    // So Ata (Yssaril commander): look at the active player's action cards (information only: for the human player)
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !G.players[q].human || !commanderUsable(G, q, "yssaril") || !hasUnits(G, q, sys)) continue;
        // (fixes 26, the Yssaril review: "You may look at that player's action cards, promissory notes, or secret
        // objectives" - the choice of the three; it showed only the action cards)
        int what = 1;
        if (G.fixes >= 26) {
            what = fpick(q, Ab::YssarilCommander, "So Ata: look at " + pn(p) + "'s cards?",
                         {{1, "Action cards", std::to_string(G.players[p].actionCards.size()) + " in hand"},
                          {2, "Promissory notes", std::to_string(G.players[p].notes.size()) + " in hand"},
                          {3, "Secret objectives", std::to_string(G.players[p].secrets.size()) + " unscored"},
                          {0, "Don't look", ""}},
                         leaderText(findLeader("yssarilcommander")));
            if (what == 0) continue;
        } else if (!fyes(q, Ab::YssarilCommander, "So Ata: look at " + pn(p) + "'s action cards?", leaderText(findLeader("yssarilcommander")))) {
            continue;
        }
        E.revealed();
        std::vector<Option> opts;
        int i = 0;
        if (what == 1)
            for (int16_t c : G.players[p].actionCards) opts.push_back({i++, kActionCards[c].name, actionCardText(c)});
        else if (what == 2)
            for (int16_t n : G.players[p].notes) opts.push_back({i++, noteNameOf(G, n) + " (" + pn(noteOwner(n)) + ")", noteTextOf(G, n)});
        else
            for (int16_t o : G.players[p].secrets) opts.push_back({i++, kObjectives[o].name, objectiveSummary(o)});
        if (opts.empty()) opts.push_back({0, what == 1 ? "No action cards" : what == 2 ? "No promissory notes" : "No secret objectives", ""});
        fpick(q, Ab::YssarilCommander,
              "So Ata: " + pn(p) + (what == 1 ? "'s action cards" : what == 2 ? "'s promissory notes" : "'s secret objectives"), opts,
              "Just for your information.");
    }
    // Dirzuga Rophal (Arborec commander): produce 1 unit where another player activated
    for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
        if (q == p || !commanderUsable(G, q, "arborec") || !hasProductionIn(G, q, sys) || spendable(G, q, false) <= 0) continue;
        if (!fyes(q, Ab::ArborecCommander, "Dirzuga Rophal: produce 1 unit in " + sysName(sys) + "?", pn(p) + " activated it.")) continue;
        say(q, pn(q) + " produces in " + sysName(sys) + " (Dirzuga Rophal).", sys);
        notProductionAbility_ = true;
        production(q, sys, 1, false);
        notProductionAbility_ = false;
    }
    // Duha Menaimon (Arborec flagship): produce up to 5 units in its system
    if (flagshipAbility(G, p, "arborec") && flagshipIn(G, p, sys) && spendable(G, p, false) > 0 &&
        fyes(p, Ab::ArborecFlagship, "Duha Menaimon: produce up to 5 units in " + sysName(sys) + "?", unitAbilityText(G.players[p].unit[int(UnitType::Flagship)]))) {
        say(p, pn(p) + " produces in " + sysName(sys) + " (Duha Menaimon).", sys);
        G.fx.anyPlanetGF = true;
        notProductionAbility_ = true;
        production(p, sys, 5, false);
        notProductionAbility_ = false;
        G.fx.anyPlanetGF = false;
    }
    // I48S (L1Z1X agent): swap 1 infantry in the active system for a mech
    // (fixes 26, the L1Z1X review - owner 2026-11-12) "allow that player to replace 1 of their infantry": the L1Z1X may use
    // it on another player's activation - a person playing them; bots only for their own (or a promised one)
    if (G.fixes >= 26) {
        const int Lz = playerOfFaction(G, "l1z1x");
        if (Lz >= 0 && Lz != p && canUseAgent(Lz, "l1z1x") && (G.players[size_t(Lz)].human || G.agentFor[size_t(Lz)] == p) &&
            unitsAvailable(G, p, UnitType::Mech) > 0) {
            std::vector<Option> opts;
            if (G.systems[sys].space[p].count(UnitType::Infantry)) opts.push_back({-2, "Infantry in space", ""});
            for (int16_t x : G.systems[sys].planets)
                if (G.planets[x].units[p].count(UnitType::Infantry) && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
            if (!opts.empty() && fyes(Lz, Ab::L1Z1XAgent, "Exhaust I48S for " + pn(p) + ": they replace 1 of their infantry here with a mech?",
                                      leaderText(findLeader("l1z1xagent")))) {
                int where = opts.size() == 1 ? opts[0].id : fpick(p, Ab::L1Z1XAgent, "I48S: replace which infantry with a mech?", opts);
                spendAgent(Lz, "l1z1x");
                Force& f = where == -2 ? G.systems[sys].space[p] : G.planets[where].units[p];
                f.remove(UnitType::Infantry, 1, false);
                f.add(UnitType::Mech, 1);
                say(p, pn(p) + " swaps an infantry for a mech (" + pn(Lz) + "'s I48S).", sys);
            }
        }
    }
    if (canUseAgent(p, "l1z1x") && unitsAvailable(G, p, UnitType::Mech) > 0) {
        std::vector<Option> opts;
        if (G.systems[sys].space[p].count(UnitType::Infantry)) opts.push_back({-2, "Infantry in space", ""});
        for (int16_t x : G.systems[sys].planets)
            if (G.planets[x].units[p].count(UnitType::Infantry) && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (!opts.empty() &&
            fyes(p, Ab::L1Z1XAgent, "Exhaust I48S to swap 1 infantry here for a mech?", leaderText(findLeader("l1z1xagent")))) {
            int where = opts.size() == 1 ? opts[0].id : fpick(p, Ab::L1Z1XAgent, "I48S: swap which infantry?", opts);
            spendAgent(p, "l1z1x");
            Force& f = where == -2 ? G.systems[sys].space[p] : G.planets[where].units[p];
            f.remove(UnitType::Infantry, 1, false);
            f.add(UnitType::Mech, 1);
            say(p, pn(p) + " swaps an infantry for a mech (I48S).", sys);
        }
    }
}

void Rules::afterMoveFactions(int p, int sys) {
    if (!hasShips(G, p, sys)) return;
    // Stymie (Arborec's note, original version): after the Arborec player moves ships into a system with your units
    // (Codex, 0.31.0 - Stymie Omega: after any other player does; it is the mover's command token that goes down)
    const int arb = playerOfFaction(G, "arborec");
    if (codexOn(G) && arb >= 0 && !G.players[arb].eliminated)
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || q == arb || !hasUnits(G, q, sys) || !holdsNote(G, q, arb, kNoteFaction)) continue;
            if (reinforcementTokens(p) <= 0) break;
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const SystemState& sy = G.systems[s];
                if (sy.home >= 0 || sy.hasToken(p) || sy.isHyperlane() || sy.offBoard) continue;
                int ships = sy.space[p].ships();
                opts.push_back({int(s), sysName(int(s)), ships ? std::to_string(ships) + " of their ships" : ""});
            }
            if (opts.empty()) break;
            opts.push_back({-1, "Don't play it", ""});
            int s = fpick(q, Ab::Stymie, "Play Stymie: put one of " + pn(p) + "'s command tokens in which system?", opts,
                          factionNoteText(G.players[arb].faction, kNoteFaction), true);
            if (s < 0) continue;
            G.systems[s].tokens |= uint16_t(1u << p);
            returnNote(q, makeNote(arb, kNoteFaction));
            abilityEvent2(E, q, "Stymie", pn(q) + " plays Stymie: a command token of " + pn(p) + " goes to " + sysName(s) + ".", s);
            zeuTE(p, s);  // (fixes 27)
            break;
        }
    if (fac(p, "arborec") && !codexOn(G))
        for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
            if (q == p || !hasUnits(G, q, sys) || !holdsNote(G, q, p, kNoteFaction)) continue;
            if (reinforcementTokens(p) <= 0) break;
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const SystemState& sy = G.systems[s];
                if (sy.home >= 0 || sy.hasToken(p) || sy.isHyperlane() || sy.offBoard) continue;
                int ships = sy.space[p].ships();
                opts.push_back({int(s), sysName(int(s)), ships ? std::to_string(ships) + " Arborec ships" : ""});
            }
            if (opts.empty()) break;
            opts.push_back({-1, "Don't play it", ""});
            int s = fpick(q, Ab::Stymie, "Play Stymie: put an Arborec command token in which system?", opts,
                          factionNoteText(G.players[p].faction, kNoteFaction), true);
            if (s < 0) continue;
            G.systems[s].tokens |= uint16_t(1u << p);
            returnNote(q, makeNote(p, kNoteFaction));
            abilityEvent2(E, q, "Stymie", pn(q) + " plays Stymie: an Arborec command token goes to " + sysName(s) + ".", s);
            zeuTE(p, s);  // (fixes 27)
            break;
        }
    // Foresight (Naalu): their ships slip away from the player who moved in
    for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
        if (q == p || !fac(q, "naalu") || !hasShips(G, q, sys) || G.players[q].strategy <= 0) continue;
        std::vector<Option> opts;
        for (int16_t a : G.systems[sys].adj) {
            const SystemState& s = G.systems[a];
            if (s.isHyperlane()) continue;
            uint8_t an = s.d().anomalies;
            if (an & kSupernova) continue;
            if ((an & kAsteroid) && !owns(q, "amd")) continue;
            // (fixes 27, the Naalu review - LRR 59.2) ships move into a nebula only when it is the active system
            if (G.fixes >= 27 && (anomaliesFor(G, q, a) & kNebula)) continue;
            if (otherShipsIn(G, q, a)) continue;
            opts.push_back({a, sysName(a), ""});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "Stay and fight", ""});
        int to = fpick(q, Ab::Foresight, "Foresight: spend 1 strategy token to move your ships out of " + sysName(sys) + "?", opts,
                       pn(p) + " just moved in.", true);
        if (to < 0) continue;
        spendStrategy(q, 1);
        if (!G.systems[to].hasToken(q)) G.systems[to].tokens |= uint16_t(1u << q);
        if (G.fixes >= 27) foresightPickup(q, sys, to);  // (the ground forces it takes along go with the ships below)
        Force& src = G.systems[sys].space[q];
        Force& dst = G.systems[to].space[q];
        for (int t = 0; t < kUnitTypes; ++t) {
            if (isStructure(UnitType(t))) continue;  // a Saar floating factory can't move
            dst.n[t] = uint8_t(dst.n[t] + src.n[t]);
            dst.dmg[t] = uint8_t(dst.dmg[t] + src.dmg[t]);
            src.n[t] = src.dmg[t] = 0;
        }
        G.players[q].usedFactionAbility = true;
        abilityEvent2(E, q, "Foresight", pn(q) + "'s ships slip away to " + sysName(to) + " (Foresight).", to);
        zeuTE(q, to);  // (fixes 27: a command token placed - Thunder's Edge's Z'eu)
        removeExcessCapacity(q, to);
        removeExcessCapacity(q, sys);
        checkNexus(to);
        if (G.fixes >= 27) checkFloatingFactories(to);  // (fixes 27, the Saar review: a lone floating factory there is blockaded)
    }
}

// (fixes 27, the Naalu review - LRR FAQ: "Any effect that moves a ship allows that ship to transport units in its system, if
// it has available capacity") the Naalu's ships slipping away may take their ground forces from the planets along; they
// go into the space area first and move with the ships
void Rules::foresightPickup(int q, int from, int to) {
    (void)to;
    Force& src = G.systems[size_t(from)].space[size_t(q)];
    const int used = src.groundForces() + (fightersFree(G, q) ? 0 : src.count(UnitType::Fighter));
    const int room = forceCapacity(G, q, src) - used;
    if (room <= 0) return;
    std::vector<Option> opts;
    for (int16_t x : G.systems[size_t(from)].planets) {
        const Force& f = G.planets[size_t(x)].units[size_t(q)];
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            for (int i = 0; i < f.count(t); ++i) opts.push_back({int(x) * 64 + int(t) * 8 + std::min(i, 7), std::string(unitTypeName(t)) + " on " + planetName(x), ""});
    }
    if (opts.empty()) return;
    const std::vector<int> picks = fpickMany(q, Ab::Foresight, "Foresight: take which ground forces along?", opts, 0,
                                             std::min<int>(room, int(opts.size())), "Up to " + std::to_string(room) + " (the ships' free capacity).");
    int moved = 0;
    for (int id : picks) {
        const int x = id / 64;
        const UnitType t = UnitType((id % 64) / 8);
        Force& f = G.planets[size_t(x)].units[size_t(q)];
        if (f.count(t) <= 0) continue;
        const bool dmg = f.dmg[int(t)] > 0 && f.dmg[int(t)] >= f.count(t);
        f.remove(t, 1, dmg);
        src.add(t, 1);
        if (dmg) ++src.dmg[int(t)];
        ++moved;
    }
    if (moved) say(q, pn(q) + " takes " + std::to_string(moved) + (moved == 1 ? " ground force" : " ground forces") + " along (Foresight).", from);
}

// (fixes 27, the Naalu review - owner 2026-11-13) Thunder's Edge's Z'eu: "After any player's command token is placed in a
// system: You may exhaust this card to return that token to that player's reinforcements." The Naalu decide: on their own
// token they are asked; on another player's a person holding it is asked (zeuQuiet_), a bot only when it promised the agent
void Rules::zeuTE(int who, int sys) {
    if (!teNaalu(G) || zeuBusy_ || who < 0 || who >= G.numPlayers || sys < 0 || !G.systems[size_t(sys)].hasToken(who)) return;
    const std::string text = leaderText(findLeader("naaluagent"));
    int user = -1;
    for (int o : initiativeOrder(G)) {
        if (!canUseAgent(o, "naalu")) continue;
        const bool own = o == who;
        const bool promised = G.agentFor[o] == who;
        const bool human = G.players[size_t(o)].human;
        if (!own && !promised && !(human && zeuQuiet_[o] != G.round)) continue;
        if (!own) agentFor(o, who);
        std::vector<Option> yn = {{1, "Yes", ""}, {0, "No", ""}};
        if (human && !own && !promised) yn.push_back({3, "No, and don't ask again this round", "You'll be asked again next round."});
        const int cv = curValue_;
        curValue_ = sys;
        const int c = fpick(o, Ab::NaaluAgent,
                            own ? "Exhaust Z'eu to return your command token in " + sysName(sys) + " to your reinforcements?"
                                : "Exhaust Z'eu: return " + pn(who) + "'s command token in " + sysName(sys) + " to their reinforcements?",
                            yn, text);
        curValue_ = cv;
        curFor_ = -1;
        if (c == 3) zeuQuiet_[o] = int16_t(G.round);
        if (c != 1) continue;
        user = o;
        break;
    }
    if (user < 0) return;
    zeuBusy_ = true;
    spendAgent(user, "naalu");
    if (user != who) agentDone(user, who);
    G.systems[size_t(sys)].tokens = uint16_t(G.systems[size_t(sys)].tokens & ~(1u << who));
    say(who, pn(who) + "'s command token in " + sysName(sys) + " goes back to their reinforcements (Z'eu).", sys);
    zeuBusy_ = false;
}

// (fixes 27, the Naalu review - owner 2026-11-13) Thunder's Edge's Iconoclast: "DEPLOY: When another player gains a relic,
// place 1 mech on any planet you control."
void Rules::iconoclastTE(int gainer) {
    if (!teNaalu(G) || !mechAbilities(G) || G.phase == Phase::Setup) return;
    for (int n : initiativeOrder(G)) {
        if (n == gainer || !fac(n, "naalu") || G.players[size_t(n)].eliminated || unitsAvailable(G, n, UnitType::Mech) <= 0) continue;
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, n))
            if (!G.planets[size_t(x)].destroyed && !isDMZ(G, x)) opts.push_back({x, planetName(x), ""});
        if (opts.empty()) continue;
        opts.push_back({-1, "Don't deploy it", ""});
        const int x = fpick(n, Ab::NaaluMechTE, "Deploy Iconoclast: " + pn(gainer) + " gained a relic - place 1 mech on which of your planets?", opts,
                            unitAbilityText(G.players[size_t(n)].unit[int(UnitType::Mech)]), false, true);
        if (x < 0) continue;
        addUnits(n, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
        abilityEvent2(E, n, "Iconoclast", pn(n) + " deploys Iconoclast on " + planetName(x) + " (" + pn(gainer) + " gained a relic).", G.planets[size_t(x)].system);
    }
}

bool Rules::assimilate(int planet, int p, int old) {
    if (!fac(p, "l1z1x") || old < 0 || old == p) return false;
    PlanetState& pl = G.planets[planet];
    if (!pl.units[old].structures()) return false;
    removing_ = true;
    for (UnitType t : {UnitType::PDS, UnitType::SpaceDock}) {
        int k = pl.units[old].count(t);
        if (!k) continue;
        destroyUnits(old, t, k, pl.system, planet, p, "assimilated");
        addUnits(p, t, std::min(k, unitsAvailable(G, p, t)), pl.system, planet);
    }
    removing_ = false;
    abilityEvent2(E, p, "Assimilate", pn(p) + " assimilates the structures on " + planetName(planet) + ".", pl.system);
    return true;
}

bool Rules::fightersLand(int p, int sys) const {
    return flagshipAbility(G, p, "naalu") && flagshipIn(G, p, sys) && sys == G.activeSystem && p == G.activePlayer;
}

void Rules::returnLandedFighters(int p, int sys) {
    int n = 0;
    for (int16_t x : G.systems[sys].planets) {
        Force& f = G.planets[x].units[p];
        int k = f.count(UnitType::Fighter);
        if (!k) continue;
        int dk = f.dmg[int(UnitType::Fighter)];
        f.n[int(UnitType::Fighter)] = f.dmg[int(UnitType::Fighter)] = 0;
        Force& sp = G.systems[sys].space[p];
        sp.add(UnitType::Fighter, k);
        sp.dmg[int(UnitType::Fighter)] = uint8_t(sp.dmg[int(UnitType::Fighter)] + dk);
        n += k;
    }
    if (n) say(p, pn(p) + ": " + std::to_string(n) + " fighter(s) return to space (Matriarch).", sys);
}

void Rules::dannelInvade(int p, const std::vector<int>& picks) {
    std::vector<std::pair<int, int>> per;
    for (int x : picks) {
        bool found = false;
        for (auto& e : per)
            if (e.first == x) ++e.second, found = true;
        if (!found) per.push_back({x, 1});
    }
    // one planet at a time: land, fight, take control (owner: RULES-CHECK 0.7.0 #8)
    for (auto& e : per) {
        const int x = e.first, sys = G.planets[x].system;
        int k = std::min(e.second, unitsAvailable(G, p, UnitType::Infantry));
        if (k <= 0 || G.planets[x].destroyed) continue;
        addUnits(p, UnitType::Infantry, k, sys, x);
        event(EventKind::Land, p, pn(p) + " lands " + std::to_string(k) + " infantry on " + planetName(x) + " (Dannel of the Tenth).", sys, x);
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.planets[x].units[p].groundForces() > 0 && G.planets[x].units[q].groundForces() > 0) groundCombat(p, q, x);
        // (fixes 26, the Yin review) neutral ground forces (Thunder's Edge) are fought too - they were "captured" untouched
        const bool v26 = G.fixes >= 26;
        if (v26 && G.planets[x].units[p].groundForces() > 0 && neutralGroundOn(G, x) > 0) groundCombat(p, kNeutral, x);
        bool contested = false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.planets[x].units[q].groundForces() > 0) contested = true;
        if (v26 && neutralGroundOn(G, x) > 0) contested = true;
        if (!contested && G.planets[x].units[p].groundForces() > 0 && G.planets[x].owner != p) setControl(x, p);
        if (v26) checkNexus(sys);  // (a unit placed in the wormhole nexus flips it - LRR 100.2)
    }
}

// ------------------------------------------------------------------ combat
void Rules::spaceCombatStart2(int att, int def, int sys) {
    destroyedShipTypes_ = 0;
    combatSys_ = sys;
    // (fixes 26, the Vuil'raith review - owner 2026-11-12) the two sides from the very start of the combat: Ambush, Impulse
    // Core and the like destroy units before spaceCombatStart3, and the last combat's sides were still set (Devour captured
    // in other players' combats; missed in the Vuil'raith's own)
    if (G.fixes >= 26) combatSide_[0] = att, combatSide_[1] = def;
    ipswitch_ = -1;
    // Ipswitch, Loose Cannon (Mentak hero)
    for (int p : {att, def})
        if (fac(p, "mentak") && heroUnlocked(G, p) && ipswitch_ < 0 &&
            fyes(p, Ab::MentakHero, "Use Ipswitch, Loose Cannon in this combat?", leaderText(findLeader("mentakhero")))) {
            purgeHero(p);
            ipswitch_ = p;
            heroEvent2(E, G, p, pn(p) + " unleashes Ipswitch, Loose Cannon.");
        }
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        const int o = p == att ? def : att;
        Force& f = G.systems[sys].space[p];
        // Ambush (Mentak): up to 2 cruisers / destroyers roll before the combat
        // (fixes 27: The Alastor's ground forces are ships for these - shipsOrAlastor / alastorMask, the owner 2026-11-14)
        if (fac(p, "mentak") && f.count(UnitType::Cruiser) + f.count(UnitType::Destroyer) > 0 && shipsOrAlastor(o, sys)) {
            std::vector<Option> opts;
            for (UnitType t : {UnitType::Cruiser, UnitType::Destroyer})
                for (int k = 0; k < std::min(2, f.count(t)); ++k)
                    opts.push_back({int(t) * 16 + k, unitTypeName(t), "hits on " + std::to_string(unitDef(G, p, t).combat)});
            std::vector<int> picks = fpickMany(p, Ab::Ambush, "Ambush: which ships roll? (up to 2)", opts, 0,
                                               std::min<int>(2, int(opts.size())), abilityText(G.players[p].faction, 0));
            if (!picks.empty()) {
                Force rollers;
                for (int id : picks) rollers.add(UnitType(id / 16), 1);
                RollResult r = rollUnits(p, rollers, RollKind::Combat, 0, false);
                reportRoll(p, sys, "Ambush", r);
                G.players[p].usedFactionAbility = true;
                if (r.hits) assignHits(o, sys, -1, r.hits, kShipMask | alastorMask(o), "Ambush", p);
            }
        }
        // Impulse Core (Yin): destroy a cruiser / destroyer for 1 hit on a non-fighter ship
        if (owns(p, "ic") && f.count(UnitType::Cruiser) + f.count(UnitType::Destroyer) > 0 && shipsOrAlastor(o, sys)) {
            std::vector<Option> opts;
            for (UnitType t : {UnitType::Cruiser, UnitType::Destroyer})
                if (f.count(t)) opts.push_back({int(t), std::string("Destroy a ") + unitTypeName(t), "1 hit on " + pn(o) + "'s ships"});
            opts.push_back({-1, "Don't use it", ""});
            int t = fpick(p, Ab::ImpulseCore, "Impulse Core: destroy one of your ships for 1 hit on a non-fighter?", opts,
                          techSummary(findTech("ic")));
            if (t >= 0) {
                destroyUnits(p, UnitType(t), 1, sys, -1, -1, "Impulse Core");
                int nf = maxAbsorbableHits(G, o, sys, -1, kNonFighter | alastorMask(o));
                assignHits(o, sys, -1, 1, (nf > 0 ? kNonFighter : kShipMask) | alastorMask(o), "Impulse Core", p);
            }
        }
    }
}

void Rules::devotion(int att, int def, int sys) {
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        const int o = p == att ? def : att;
        if (!fac(p, "yin") || !shipsOrAlastor(o, sys)) continue;
        const Force& f = G.systems[sys].space[p];
        std::vector<Option> opts;
        for (UnitType t : {UnitType::Cruiser, UnitType::Destroyer})
            if (f.count(t)) opts.push_back({int(t), std::string("Destroy a ") + unitTypeName(t), "1 hit on " + pn(o) + "'s ships"});
        if (opts.empty()) continue;
        opts.push_back({-1, "No", ""});
        int t = fpick(p, Ab::Devotion, "Devotion: destroy one of your ships to make 1 hit?", opts, abilityText(G.players[p].faction, 1));
        if (t < 0) continue;
        destroyUnits(p, UnitType(t), 1, sys, -1, -1, "Devotion");
        G.players[p].usedFactionAbility = true;
        abilityEvent2(E, p, "Devotion", pn(p) + " sacrifices a " + unitTypeName(UnitType(t)) + " (Devotion).", sys);
        if (!shipsOrAlastor(o, sys)) continue;
        if (G.fixes < 26) {
            assignHits(o, sys, -1, 1, kShipMask, "Devotion", p);
            continue;
        }
        // (fixes 26, the Yin review - owner 2026-11-12) "produce 1 hit and assign it to 1 of your opponent's ships in that
        // system": the Yin choose the ship (it was the opponent); a damaged one is a choice of its own (no sustaining it)
        const Force& of = G.systems[sys].space[o];
        std::vector<Option> ts;
        std::vector<int> kinds(std::begin(kShipIdx), std::end(kShipIdx));
        if (alastorMask(o)) kinds.insert(kinds.end(), {int(UnitType::Infantry), int(UnitType::Mech)});  // (fixes 27)
        for (int u : kinds) {
            const int dm = of.dmg[u], fresh = of.n[u] - dm;
            if (fresh > 0) ts.push_back({u, unitTypeName(UnitType(u)), std::to_string(fresh) + (dm ? " undamaged" : "")});
            if (dm > 0) ts.push_back({fresh > 0 ? 64 + u : u, std::string(unitTypeName(UnitType(u))) + ", damaged", std::to_string(dm) + " damaged"});
        }
        if (ts.empty()) continue;
        const int cf = curFor_;
        curFor_ = o;  // (for the AI: whose ships)
        const int u = ts.size() == 1 ? ts[0].id : fpick(p, Ab::Devotion, "Devotion: which of " + pn(o) + "'s ships takes the hit?", ts,
                                                      abilityText(G.players[p].faction, 1));
        curFor_ = cf;
        if (u >= 64) {  // a damaged ship: no sustaining it
            if (tellurian(o, sys, -1, 1, 1 << (u - 64)) <= 0) continue;
            const bool was = byHits_;
            byHits_ = true;
            destroyUnits(o, UnitType(u - 64), 1, sys, -1, p, "Devotion");
            byHits_ = was;
        } else {
            assignHits(o, sys, -1, 1, 1 << u, "Devotion", p);
        }
    }
}

int Rules::restrictedHits(int p, int sys, const RollResult& r) const {
    // [0.0.1] (L1Z1X flagship): hits by it and the dreadnoughts in its system go to non-fighter ships if able
    if (!flagshipAbility(G, p, "l1z1x") || !flagshipIn(G, p, sys)) return 0;
    int n = 0;
    for (size_t i = 0; i < r.dice.size() && i < r.unit.size(); ++i)
        if (r.dice[i] >= r.need[i] && (r.unit[i] == int(UnitType::Flagship) || r.unit[i] == int(UnitType::Dreadnought))) ++n;
    return std::min(n, r.hits);
}

void Rules::spaceCombatEnded(int att, int def, int sys, int winner) {
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        if (G.players[p].eliminated || !owns(p, "so")) continue;
        // Salvage Operations (Mentak): a trade good after the combat, won or lost; the winner may rebuild a lost type
        if (G.fixes >= 9 && winner < 0) continue;  // (0.44.5: a draw has no winner and no loser, LRR 18)
        gainTG(p, 1);
        say(p, pn(p) + " gains 1 trade good (Salvage Operations).", sys);
        if (winner != p || !destroyedShipTypes_) continue;
        std::vector<Option> opts;
        for (int t : kShipIdx) {
            if (!((destroyedShipTypes_ >> t) & 1) || unitsAvailable(G, p, UnitType(t)) <= 0 || unitCost2(G, p, UnitType(t)) <= 0) continue;
            int cost = unitCost(G, p, UnitType(t));
            if (cost > spendable(G, p, false)) continue;
            opts.push_back({t, unitTypeName(UnitType(t)), std::to_string(cost) + " resources"});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "No", ""});
        int t = fpick(p, Ab::SalvageOps, "Salvage Operations: produce 1 ship here?", opts, techSummary(findTech("so")), false);
        if (t < 0 || !pay(p, unitCost(G, p, UnitType(t)), false, "Salvage Operations", false)) continue;
        addUnits(p, UnitType(t), 1, sys, -1);
        say(p, pn(p) + " produces a " + unitTypeName(UnitType(t)) + " (Salvage Operations).", sys);
        if (G.modules & kModThundersEdge) phantomProducedOne(p, sys, UnitType(t), 1);  // Homesick Phantom (fixes 24)
        removeExcessCapacity(p, sys);
    }
    // S'ula Mentarion (Mentak commander): the loser gives the winner a promissory note
    if (winner >= 0) {
        const int loser = winner == att ? def : att;
        std::vector<Option> opts;
        if (loser < G.numPlayers && commanderUsable(G, winner, "mentak") && !G.players[loser].eliminated) opts = notesToGive(loser, winner);
        if (!opts.empty()) {
            int n = opts.size() == 1 ? opts[0].id
                                     : pick(loser, Tag::GiveNote, "S'ula Mentarion: give " + pn(winner) + " which promissory note?", opts);
            say(loser, pn(loser) + " must give " + pn(winner) + " a promissory note (S'ula Mentarion).", sys);
            giveNote(loser, winner, n);
        }
    }
    ipswitch_ = -1;
    combatSys_ = -1;
    destroyedShipTypes_ = 0;
}

void Rules::groundCombatStart2(int att, int def, int planet) {
    if (G.fixes >= 26) combatSide_[0] = att, combatSide_[1] = def;  // (fixes 26, the Vuil'raith review: as spaceCombatStart2)
    const int sys = G.planets[planet].system;
    PlanetState& ps = G.planets[planet];
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        const int o = p == att ? def : att;
        // Indoctrination (Yin): 2 influence to turn 1 of the opponent's infantry (Moyin's Ashes: 3 for a mech)
        // (fixes 26, the Yin review - owner 2026-11-12) with no infantry left in the reinforcements, the mech version still
        // can be used (it needed a free infantry)
        const bool moyin = unitsAvailable(G, p, UnitType::Mech) > 0 && spendable(G, p, true) >= 3 && mechAbilities(G);
        const bool infFree = unitsAvailable(G, p, UnitType::Infantry) > 0;
        if (fac(p, "yin") && ps.units[o].count(UnitType::Infantry) > 0 && spendable(G, p, true) >= 2 && (infFree || (G.fixes >= 26 && moyin))) {
            std::vector<Option> opts;
            if (infFree || G.fixes < 26) opts.push_back({0, "Swap for 1 infantry", "2 influence"});
            if (unitsAvailable(G, p, UnitType::Mech) > 0 && spendable(G, p, true) >= 3 && mechAbilities(G))
                opts.push_back({1, "Swap for 1 mech (Moyin's Ashes)", "3 influence"});
            opts.push_back({-1, "No", ""});
            int c = fpick(p, Ab::Indoctrination, "Indoctrination: convert 1 of " + pn(o) + "'s infantry?", opts,
                          abilityText(G.players[p].faction, 0));
            if (c >= 0 && pay(p, c == 1 ? 3 : 2, true, "Indoctrination", false)) {
                removing_ = true;
                destroyUnits(o, UnitType::Infantry, 1, sys, planet, p, "Indoctrination");
                removing_ = false;
                addUnits(p, c == 1 ? UnitType::Mech : UnitType::Infantry, 1, sys, planet);
                G.players[p].usedFactionAbility = true;
                abilityEvent2(E, p, "Indoctrination", pn(p) + " converts one of " + pn(o) + "'s infantry (Indoctrination).", sys);
            }
        }
        // Greyfire Mutagen (Yin's note): against 2 or more ground forces that aren't Yin's
        const int y = playerOfFaction(G, "yin");
        if (y >= 0 && y != p && o != y && holdsNote(G, p, y, kNoteFaction) && ps.units[o].groundForces() >= 2 &&
            ps.units[o].count(UnitType::Infantry) > 0 && unitsAvailable(G, p, UnitType::Infantry) > 0 &&
            fyes(p, Ab::Greyfire, "Play Greyfire Mutagen: swap 1 of " + pn(o) + "'s infantry for 1 of yours?",
                 factionNoteText(G.players[y].faction, kNoteFaction))) {
            removing_ = true;
            destroyUnits(o, UnitType::Infantry, 1, sys, planet, p, "Greyfire Mutagen");
            removing_ = false;
            addUnits(p, UnitType::Infantry, 1, sys, planet);
            returnNote(p, makeNote(y, kNoteFaction));
            abilityEvent2(E, p, "Greyfire Mutagen", pn(p) + " plays Greyfire Mutagen.", sys);
        }
    }
}

void Rules::harrow(int att, int def, int planet) {
    const int sys = G.planets[planet].system;
    if (sys != G.activeSystem) return;
    for (int p : {att, def}) {
        if (p >= G.numPlayers) continue;  // (Thunder's Edge: neutral units have no abilities)
        const int o = p == att ? def : att;
        if (!fac(p, "l1z1x") || G.planets[planet].units[o].groundForces() == 0) continue;
        if (lawInPlay(G, "conventions") && hasTraitOf(G, planet, Trait::Cultural)) continue;  // Conventions of War
        Force guns;
        bool ignoreShield = commanderUsable(G, p, "l1z1x");  // 2RAM
        const Force& sp = G.systems[sys].space[p];
        for (int t : kShipIdx) {
            if (!sp.n[t]) continue;
            const UnitDef& u = unitDef(G, p, UnitType(t));
            if (u.ignoresShield) ignoreShield = true;
            if (u.bombard > 0) guns.add(UnitType(t), sp.n[t]);
        }
        // (fixes 26, the L1Z1X review - owner 2026-11-12) Annihilator: a mech not in this ground combat - on another planet of
        // the system - "can use its BOMBARDMENT ability on planets in its system as if it were a ship"
        if (G.fixes >= 26 && mechAbilities(G) && !std::strcmp(unitDef(G, p, UnitType::Mech).key, "l1z1x_mech")) {
            int n = 0;
            for (int16_t x : G.systems[sys].planets)
                if (x != planet) n += G.planets[x].units[p].count(UnitType::Mech);
            if (n > 0) guns.add(UnitType::Mech, n);
        }
        if (guns.empty()) continue;
        bool shield = false;
        for (int t = 0; t < kUnitTypes; ++t)
            if (G.planets[planet].units[o].n[t] && unitDef(G, o, UnitType(t)).shield) shield = true;
        if (G.fixes >= 12 && G.fx.dsShieldPlanet == planet) shield = true;  // (0.44.8: Orbital Defense Grid)
        if (shieldsDown(G, o, sys)) shield = false;
        if (shield && !ignoreShield) continue;
        if (!fyes(p, Ab::Harrow, "Harrow: bombard " + pn(o) + "'s ground forces on " + planetName(planet) + "?",
                  abilityText(G.players[p].faction, 1)))
            continue;
        RollResult r = rollUnits(p, guns, RollKind::Bombard, 0, owns(p, "ps"));
        reportRoll(p, sys, "Harrow (bombardment of " + planetName(planet) + ")", r);
        afterUnitRoll(p, o, sys, r, "bombardment");
        G.players[p].usedFactionAbility = true;
        // 0.44.6 (fixes 10): Harrow is a use of BOMBARDMENT - X-89 Bacterial Weapon (either version) and Proxima
        // Targeting VI apply as they do to the bombardment step
        const bool v10 = G.fixes >= 10;
        if (v10 && x89Omega(G, p) && r.hits > 0) {
            r.hits *= 2;
            say(p, pn(p) + "'s bombardment hits are doubled (X-89 Bacterial Weapon): " + std::to_string(r.hits) + ".", sys);
        }
        if (v10) r.hits = proximaCancel(o, sys, planet, r.hits);
        const int infBefore = G.planets[size_t(planet)].units[size_t(o)].count(UnitType::Infantry);
        assignHits(o, sys, planet, r.hits, kGroundMask, "Harrow", p);
        if (v10) {
            const int infAfter = G.planets[size_t(planet)].units[size_t(o)].count(UnitType::Infantry);
            if (x89Omega(G, p) && !G.planets[size_t(planet)].exhausted) {
                G.planets[size_t(planet)].exhausted = true;
                say(p, planetName(planet) + " is exhausted (X-89 Bacterial Weapon).", sys);
            }
            if (infAfter < infBefore && infAfter > 0 && owns(p, "x89_base") && !x89Omega(G, p) &&
                yesNo(p, Tag::UseTech, "Use X-89 Bacterial Weapon?", "Destroy all of their infantry on " + planetName(planet) + "."))
                destroyUnits(o, UnitType::Infantry, infAfter, sys, planet, p, "X-89");
        }
    }
}

void Rules::yinSpinner(int p, int sys) {
    if (!owns(p, "yso") || unitsAvailable(G, p, UnitType::Infantry) <= 0) return;
    if (codexOn(G)) {  // Yin Spinner Omega (0.31.0): up to 2, on any planet of yours or with any of your ships
        for (int k = 0; k < 2 && unitsAvailable(G, p, UnitType::Infantry) > 0; ++k) {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, p))
                if (!isDMZ(G, x) && !G.planets[x].destroyed) opts.push_back({x, planetName(x), sysName(G.planets[x].system)});
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasShips(G, p, int(s))) opts.push_back({20000 + int(s), "Space: " + sysName(int(s)), "with your ships"});
            if (opts.empty()) return;
            opts.push_back({-1, "No more", ""});
            int x = fpick(p, Ab::YinSpinner, "Yin Spinner: put an infantry where? (" + std::to_string(2 - k) + " left)", opts);
            if (x < 0) return;
            if (x >= 20000) {
                addUnits(p, UnitType::Infantry, 1, x - 20000, -1);
                removeExcessCapacity(p, x - 20000);
                say(p, pn(p) + " puts 1 infantry in space in " + sysName(x - 20000) + " (Yin Spinner).", x - 20000);
            } else {
                addUnits(p, UnitType::Infantry, 1, G.planets[x].system, x);
                say(p, pn(p) + " puts 1 infantry on " + planetName(x) + " (Yin Spinner).", G.planets[x].system);
            }
        }
        return;
    }
    // the card without the Codex: after your units use PRODUCTION (0.44.6, fixes 10: not after Sling Relay, Chaos
    // Mapping or another ability's production)
    if (G.fixes >= 10 && notProductionAbility_) return;
    std::vector<Option> opts;
    for (int16_t x : G.systems[sys].planets)
        if (G.planets[x].owner == p && !isDMZ(G, x) && !G.planets[x].destroyed) opts.push_back({x, planetName(x), ""});
    if (opts.empty()) return;
    int x = opts.size() == 1 ? opts[0].id : fpick(p, Ab::YinSpinner, "Yin Spinner: put 1 infantry on which planet?", opts, "", false, true);
    addUnits(p, UnitType::Infantry, 1, sys, x);
    say(p, pn(p) + " puts 1 infantry on " + planetName(x) + " (Yin Spinner).", sys);
}

void Rules::unitDestroyedFactions(int p, UnitType t, int k, int sys, int planet) {
    if (k <= 0 || removing_) return;
    // Technological Singularity (Nekro): during a combat against the Nekro player
    // (fixes 27: from the start of the space combat - Assault Cannon, Ambush - singularitySys_)
    if ((planet < 0 && (sys == combatSys_ || (G.fixes >= 27 && inSpaceCombat_ && sys == singularitySys_))) || (planet >= 0 && planet == G.groundCombatPlanet))
        singularity(p, sys, planet);
    if (planet < 0 && sys >= 0 && sys == combatSys_ && isShip(t)) {
        destroyedShipTypes_ = uint16_t(destroyedShipTypes_ | (1u << int(t)));  // Salvage Operations
        // Ipswitch, Loose Cannon: a ship of that type for each of the opponent's ships destroyed
        if (ipswitch_ >= 0 && p != ipswitch_) {
            int n = std::min(k, unitsAvailable(G, ipswitch_, t));
            if (n > 0) {
                addUnits(ipswitch_, t, n, sys, -1);
                say(ipswitch_, pn(ipswitch_) + " gains " + std::to_string(n) + " " + unitTypeName(t, n > 1) + " (Ipswitch).", sys);
            }
        }
    }
    unitDestroyed4(p, t, k, sys, planet);  // Devour, The Terror Between, Reanimator, Crimson Legionnaire
    // Van Hauge (Yin flagship): when it is destroyed, every ship in its system is destroyed
    if (t == UnitType::Flagship && planet < 0 && sys >= 0 && (fac(p, "yin") || (G.fixes >= 8 && flagshipText(G, p, "yin")))) {
        abilityEvent2(E, p, "Van Hauge", pn(p) + "'s Van Hauge explodes: every ship in " + sysName(sys) + " is destroyed!", sys);
        // (fixes 26, the Yin review) "destroy all ships in this system" - the neutral ships too (Thunder's Edge)
        for (int q = 0; q < (G.fixes >= 26 ? kSides : G.numPlayers); ++q) {
            if (q >= G.numPlayers && q != kNeutral) continue;
            for (int u : kShipIdx)
                if (G.systems[sys].space[q].n[u]) destroyUnits(q, UnitType(u), G.systems[sys].space[q].n[u], sys, -1, p, "Van Hauge");
        }
    }
    // Brother Milor Omega (Yin agent, Codex 0.31.0): after any unit is destroyed in a combat, the Yin agent can give
    // its owner 2 fighters in that system (a ship) or 2 infantry on that planet (a ground force)
    if (codexOn(G)) {
        const bool ship = planet < 0 && sys >= 0 && sys == combatSys_ && isShip(t);
        const bool gf = planet >= 0 && planet == G.groundCombatPlanet && isGroundForce(t);
        if (ship || gf) {
            for (int y = 0; y < G.numPlayers; ++y) {
                // bots spend it on their own units - or for a player they promised it to (0.43.0, C4)
                if (!canUseAgent(y, "yin") || (y != p && !G.players[y].human && !(G.fixes >= 2 && G.agentFor[y] == p))) continue;
                // 0.58.2 (fixes 21, tester: "tried to use the Yin agent on myself and it went to the opponent"): a person
                // isn't asked to give their combat opponent the units (unless it was promised in a deal) - the question
                // for the opponent's losses came first and read like the one for their own
                const bool myFight = ship ? G.systems[size_t(sys)].space[size_t(y)].ships() > 0 : groundForcesOn(G, y, planet) > 0;
                if (G.fixes >= 21 && y != p && myFight && G.agentFor[y] != p) continue;
                const UnitType give = ship ? UnitType::Fighter : UnitType::Infantry;
                if (unitsAvailable(G, p, give) <= 0) break;
                if (!fyes(y, Ab::YinAgent,
                          y == p ? "Exhaust Brother Milor: YOU get 2 " + std::string(unitTypeName(give, true)) +
                                       (ship ? " in " + sysName(sys) : " on " + planetName(planet)) + "?"
                                 : "Exhaust Brother Milor for " + pn(p) + " (not you): THEY get 2 " + unitTypeName(give, true) +
                                       (ship ? " in " + sysName(sys) : " on " + planetName(planet)) + "?",
                          std::string(y == p ? "Your " : pn(p) + "'s ") + unitTypeName(t) + " was destroyed.")) {
                    if (G.fixes >= 26) continue;  // (fixes 26, the Yin review: the next one who could use it - Ssruu - is asked)
                    break;
                }
                spendAgent(y, "yin");
                if (y != p && G.fixes >= 2) agentDone(y, p);
                const int n = std::min(2, unitsAvailable(G, p, give));
                addUnits(p, give, n, sys, ship ? -1 : planet);
                if (ship) removeExcessCapacity(p, sys);
                say(p, pn(p) + " gets " + std::to_string(n) + " " + unitTypeName(give, n > 1) + " (Brother Milor).", sys);
                break;
            }
        }
        return;
    }
    // (fixes 26, the Yin review - owner 2026-11-12) the original card: "After a player's destroyer or cruiser is destroyed:
    // You may exhaust this card; if you do, that player may place up to 2 fighters from their reinforcements in that unit's
    // system." - any player's (it was only the Yin's own); asked as the Omega is: a bot for its own ships or a player it
    // promised it to, a person for anyone's but their combat opponent's (unless promised)
    if (G.fixes >= 26) {
        if (planet >= 0 || sys < 0 || (t != UnitType::Cruiser && t != UnitType::Destroyer) || unitsAvailable(G, p, UnitType::Fighter) <= 0) return;
        for (int y = 0; y < G.numPlayers; ++y) {
            if (!canUseAgent(y, "yin") || (y != p && !G.players[y].human && G.agentFor[y] != p)) continue;
            if (y != p && G.systems[size_t(sys)].space[size_t(y)].ships() > 0 && G.agentFor[y] != p) continue;
            if (!fyes(y, Ab::YinAgent,
                      y == p ? "Exhaust Brother Milor: YOU get 2 fighters in " + sysName(sys) + "?"
                             : "Exhaust Brother Milor for " + pn(p) + " (not you): THEY get 2 fighters in " + sysName(sys) + "?",
                      std::string(y == p ? "Your " : pn(p) + "'s ") + unitTypeName(t) + " was destroyed."))
                continue;
            spendAgent(y, "yin");
            if (y != p) agentDone(y, p);
            const int n = std::min(2, unitsAvailable(G, p, UnitType::Fighter));
            addUnits(p, UnitType::Fighter, n, sys, -1);
            removeExcessCapacity(p, sys);
            say(p, pn(p) + " puts " + std::to_string(n) + " fighter" + (n > 1 ? "s" : "") + " in " + sysName(sys) + " (Brother Milor).", sys);
            break;
        }
        return;
    }
    // Brother Milor (Yin agent): 2 fighters after one of your cruisers or destroyers is destroyed
    if (planet < 0 && sys >= 0 && (t == UnitType::Cruiser || t == UnitType::Destroyer) && canUseAgent(p, "yin") &&
        unitsAvailable(G, p, UnitType::Fighter) > 0 &&
        fyes(p, Ab::YinAgent, "Exhaust Brother Milor to put 2 fighters in " + sysName(sys) + "?",
             std::string("Your ") + unitTypeName(t) + " was destroyed.")) {
        spendAgent(p, "yin");
        addUnits(p, UnitType::Fighter, 2, sys, -1);
        say(p, pn(p) + " puts 2 fighters in " + sysName(sys) + " (Brother Milor).", sys);
    }
}

// ------------------------------------------------------------------ turns and agendas
void Rules::startOfTurn2(int p) {
    // (fixes 26, the L1Z1X review - owner 2026-11-12) Cybernetic Enhancements, the original card (no Codex): "At the start of
    // your turn: Remove 1 token from the L1Z1X player's strategy pool and return it to their reinforcements. Then, place 1
    // command token from your reinforcements in your strategy pool. Then, return this card to the L1Z1X player."
    if (G.fixes >= 26 && !codexOn(G)) {
        const int L = playerOfFaction(G, "l1z1x");
        if (L >= 0 && L != p && holdsNote(G, p, L, kNoteFaction) && G.players[size_t(L)].strategy > 0 && reinforcementTokens(p) > 0 &&
            fyes(p, Ab::CyberneticEnhancements, "Play Cybernetic Enhancements: take 1 token from " + pn(L) + "'s strategy pool and gain 1 strategy token?",
                 factionNoteText(G.players[size_t(L)].faction, kNoteFaction))) {
            G.players[size_t(L)].strategy = int8_t(G.players[size_t(L)].strategy - 1);
            G.players[size_t(p)].strategy = int8_t(G.players[size_t(p)].strategy + 1);
            returnNote(p, makeNote(L, kNoteFaction));
            abilityEvent2(E, p, "Cybernetic Enhancements",
                          pn(p) + " plays Cybernetic Enhancements: " + pn(L) + " loses a strategy token, " + pn(p) + " gains one.");
        }
    }
    // Spy Net (Yssaril's note)
    const int y = playerOfFaction(G, "yssaril");
    if (y < 0 || y == p || !holdsNote(G, p, y, kNoteFaction) || G.players[y].actionCards.empty()) return;
    if (!fyes(p, Ab::SpyNet, "Play Spy Net: look at " + pn(y) + "'s action cards and take 1?", factionNoteText(G.players[y].faction, kNoteFaction)))
        return;
    E.revealed();
    std::vector<Option> opts;
    for (int16_t c : G.players[y].actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
    int c = fpick(p, Ab::SpyNet, "Spy Net: take which card?", opts);
    moveCard(G.players[y], G.players[p], c);
    returnNote(p, makeNote(y, kNoteFaction));
    abilityEvent2(E, p, "Spy Net", pn(p) + " takes an action card from " + pn(y) + " (Spy Net).");
    enforceHandLimit(p);
}

void Rules::agendaRevealed2(int agenda) {
    (void)agenda;
    // Z'eu (Naalu agent): information only - offered to the human player (Codex: Z'eu Omega is a component action)
    for (int p = 0; p < G.numPlayers && !codexOn(G); ++p) {
        if (!G.players[p].human || !canUseAgent(p, "naalu") || G.agendaDeck.empty()) continue;
        if (!fyes(p, Ab::NaaluAgent, "Exhaust Z'eu to look at the top card of the agenda deck?", leaderText(findLeader("naaluagent"))))
            continue;
        spendAgent(p, "naalu");
        const int top = G.agendaDeck.back();
        E.revealed();
        Event e;  // shown large until A, like Politics' peek
        e.kind = EventKind::AgendaPeek;
        e.player = int8_t(p);
        e.card = int16_t(top);
        e.label = "Z'eu: the next agenda";
        E.emit(e);
    }
}

// ------------------------------------------------------------------ component actions
std::vector<Option> Rules::factionActionOptions2(int p) {
    std::vector<Option> o;
    const Player& pl = G.players[p];
    auto add = [&](Ab ab, const std::string& label, const std::string& detail) { o.push_back({6000 + int(ab), label, detail}); };
    if (canUseAgent(p, "arborec") && (G.fixes >= 26 ? !osphaShips(G).empty() : !osphaOptions(G, p).empty()))
        add(Ab::ArborecAgent, "Agent: Letani Ospha",
            G.fixes >= 26 ? "Choose a player's non-fighter ship: they may replace it with one costing up to 2 more."
                          : "Swap one of your ships for one up to 2 resources more expensive.");
    for (int16_t n : pl.notes)
        if (noteType(n) == kNoteFaction && noteOwner(n) != p && isFaction(G, noteOwner(n), "mentak"))
            add(Ab::PromiseOfProtection, "Play Promise of Protection", factionNoteText(G.players[noteOwner(n)].faction, kNoteFaction));
    for (int16_t n : pl.notes)  // 0.32.0 (new games): Trade Convoys is an ACTION
        if ((G.extras & 32) && noteType(n) == kNoteFaction && noteOwner(n) != p && isFaction(G, noteOwner(n), "hacan"))
            add(Ab::TradeConvoys, "Play Trade Convoys",
                "Place it face up: you can make deals with players who aren't your neighbours. It goes back to the Hacan "
                "if you activate a system with their units.");
    if (fac(p, "yssaril") && !pl.actionCards.empty()) add(Ab::StallTactics, "Stall Tactics", "Discard 1 action card (uses your action).");
    if (techReady(p, "mi") && othersHaveCards(G, p)) add(Ab::MageonImplants, "Mageon Implants", "Look at another player's action cards and take 1.");
    if (heroUnlocked(G, p)) {
        if (fac(p, "arborec")) {
            bool gf = false;
            for (size_t s = 0; s < G.systems.size() && !gf; ++s) {
                if (G.systems[s].space[p].groundForces()) gf = true;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[x].units[p].groundForces()) gf = true;
            }
            if (gf) add(Ab::ArborecHero, "Hero: Letani Miasmiala", "Produce in every system with your ground forces. Purged afterwards.");
        }
        if (fac(p, "l1z1x") && unitsOnBoard(G, p, UnitType::Flagship) + unitsOnBoard(G, p, UnitType::Dreadnought) > 0)
            add(Ab::L1Z1XHero, "Hero: The Helmsman", "Gather your flagship and dreadnoughts in one system. Purged afterwards.");
        if (fac(p, "yin") && unitsAvailable(G, p, UnitType::Infantry) > 0)
            add(Ab::YinHero, "Hero: Dannel of the Tenth", "Land up to 3 infantry anywhere outside home systems. Purged afterwards.");
        if (fac(p, "yssaril")) add(Ab::YssarilHero, "Hero: Kyver, Blade and Key", "Take an action card from each player or make them discard. Purged afterwards.");
    }
    for (const Option& x : factionActionOptions3(p)) o.push_back(x);
    return o;
}

bool Rules::factionAction2(int p, Ab ab) {
    Player& pl = G.players[p];
    switch (ab) {
        case Ab::ArborecAgent: {
            if (G.fixes >= 26) {  // (the Arborec review - owner 2026-11-12) any player's ship; that player chooses
                std::vector<Option> ships = osphaShips(G);
                if (ships.empty()) return false;
                ships.push_back({-1, "Cancel", ""});
                const int c = fpick(p, Ab::ArborecAgent, "Letani Ospha: which player's ship?", ships, leaderText(findLeader("arborecagent")));
                if (c < 0) return false;
                const int t = c % 16, s = (c / 16) % 256, q = c / 4096;
                spendAgent(p, "arborec");
                if (q != p) say(p, pn(p) + " offers " + pn(q) + " Letani Ospha for their " + unitTypeName(UnitType(t)) + " in " + sysName(s) + ".", s);
                std::vector<Option> to;
                for (int u : osphaReplacements(G, q, t))
                    to.push_back({u, unitTypeName(UnitType(u)), std::to_string((unitCost2(G, q, UnitType(u)) + 1) / 2) + " resources instead of " +
                                                                    std::to_string((unitCost2(G, q, UnitType(t)) + 1) / 2)});
                to.push_back({-1, "Keep it", ""});
                const int u = fpick(q, Ab::ArborecAgent, "Letani Ospha: replace your " + std::string(unitTypeName(UnitType(t))) + " in " + sysName(s) + " with?", to,
                                    "A ship from your reinforcements that costs up to 2 more than the replaced ship (a damaged one is replaced first).");
                if (u < 0) {
                    say(q, pn(q) + " keeps the " + unitTypeName(UnitType(t)) + " (Letani Ospha).", s);
                    return true;
                }
                G.systems[size_t(s)].space[size_t(q)].remove(UnitType(t), 1, true);
                addUnits(q, UnitType(u), 1, s, -1);
                say(q, pn(q) + " swaps a " + unitTypeName(UnitType(t)) + " for a " + unitTypeName(UnitType(u)) + " (Letani Ospha).", s);
                removeExcessCapacity(q, s);
                return true;
            }
            std::vector<Option> opts = osphaOptions(G, p);
            if (opts.empty()) return false;
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::ArborecAgent, "Letani Ospha: upgrade which ship?", opts, leaderText(findLeader("arborecagent")));
            if (c < 0) return false;
            const int s = c / 256, t = (c / 16) % 16, u = c % 16;
            spendAgent(p, "arborec");
            G.systems[s].space[p].remove(UnitType(t), 1, false);
            addUnits(p, UnitType(u), 1, s, -1);
            say(p, pn(p) + " swaps a " + unitTypeName(UnitType(t)) + " for a " + unitTypeName(UnitType(u)) + " (Letani Ospha).", s);
            removeExcessCapacity(p, s);
            return true;
        }
        case Ab::ArborecHero: {
            purgeHero(p);
            heroEvent2(E, G, p, pn(p) + " uses Letani Miasmiala: production everywhere their ground forces are.");
            std::vector<int> done;
            for (int guard = 0; guard < 24; ++guard) {
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    if (std::find(done.begin(), done.end(), int(s)) != done.end()) continue;
                    int gf = G.systems[s].space[p].groundForces();
                    for (int16_t x : G.systems[s].planets) gf += G.planets[x].units[p].groundForces();
                    if (gf) opts.push_back({int(s), sysName(int(s)), std::to_string(gf) + " ground forces"});
                }
                if (opts.empty() || spendable(G, p, false) <= 0) break;
                opts.push_back({-1, "Done", ""});
                int s = fpick(p, Ab::ArborecHero, "Letani Miasmiala: produce in which system?", opts, "", true);
                if (s < 0) break;
                done.push_back(s);
                G.fx.anyPlanetGF = true;
                notProductionAbility_ = true;
                production(p, s, 20, false);
                notProductionAbility_ = false;
                G.fx.anyPlanetGF = false;
            }
            return true;
        }
        case Ab::L1Z1XHero: {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (!G.systems[s].isHyperlane() && !otherShipsIn(G, p, int(s)) && !(G.systems[s].d().anomalies & kSupernova))
                    opts.push_back({int(s), sysName(int(s)), ""});
            opts.push_back({-1, "Cancel", ""});
            int s = fpick(p, Ab::L1Z1XHero, "The Helmsman: gather your flagship and dreadnoughts in which system?", opts,
                          leaderText(findLeader("l1z1xhero")), true);
            if (s < 0) return false;
            std::vector<Option> ships;
            for (size_t x = 0; x < G.systems.size(); ++x) {
                if (int(x) == s) continue;
                const Force& f = G.systems[x].space[p];
                if (f.count(UnitType::Flagship)) ships.push_back({int(x) * 256 + int(UnitType::Flagship) * 16, "Flagship from " + sysName(int(x)), ""});
                for (int k = 0; k < f.count(UnitType::Dreadnought); ++k)
                    ships.push_back({int(x) * 256 + int(UnitType::Dreadnought) * 16 + k, "Dreadnought from " + sysName(int(x)), ""});
            }
            if (ships.empty()) return false;
            std::vector<int> picks = fpickMany(p, Ab::L1Z1XHero, "The Helmsman: which ships come?", ships, 1, int(ships.size()));
            purgeHero(p);
            std::vector<int> touched;
            for (int id : picks) {
                const int x = id / 256;
                const UnitType t = UnitType((id / 16) % 16);
                Force& src = G.systems[x].space[p];
                Force& dst = G.systems[s].space[p];
                bool damaged = src.dmg[int(t)] >= src.n[int(t)];  // only damaged ones left
                src.remove(t, 1, false);
                dst.add(t, 1);
                if (damaged) dst.dmg[int(t)] = uint8_t(dst.dmg[int(t)] + 1);
                if (std::find(touched.begin(), touched.end(), x) == touched.end()) touched.push_back(x);
            }
            heroEvent2(E, G, p, pn(p) + " uses The Helmsman: the fleet gathers in " + sysName(s) + ".");
            // (fixes 26, the L1Z1X review - owner 2026-11-12) LRR 95: "When a ship moves, it may transport any combination of
            // fighters and ground forces" - what would be stranded without the ships that left comes along, while they have room
            if (G.fixes >= 26)
                for (int x : touched) {
                    Force& src = G.systems[size_t(x)].space[size_t(p)];
                    Force& dst = G.systems[size_t(s)].space[size_t(p)];
                    Force moved;
                    for (int guard = 0; guard < 40 && capacityOverflow(G, p, x) > 0; ++guard) {
                        bool step = false;
                        for (UnitType t : {UnitType::Mech, UnitType::Infantry, UnitType::Fighter}) {
                            if (src.count(t) == 0 || (t == UnitType::Fighter && fightersFree(G, p))) continue;
                            const bool dmg = src.dmg[int(t)] >= src.n[int(t)];
                            src.remove(t, 1, false);
                            dst.add(t, 1);
                            if (dmg) dst.dmg[int(t)] = uint8_t(dst.dmg[int(t)] + 1);
                            if (capacityOverflow(G, p, s) > 0) {  // (no room there: back)
                                dst.remove(t, 1, dmg);
                                src.add(t, 1);
                                if (dmg) src.dmg[int(t)] = uint8_t(src.dmg[int(t)] + 1);
                                continue;
                            }
                            moved.add(t, 1);
                            step = true;
                            break;
                        }
                        if (!step) break;
                    }
                    std::string what;
                    for (UnitType t : {UnitType::Fighter, UnitType::Infantry, UnitType::Mech})
                        if (moved.count(t)) what += (what.empty() ? "" : ", ") + std::to_string(moved.count(t)) + " " + unitTypeName(t, moved.count(t) > 1);
                    if (!what.empty()) say(p, pn(p) + "'s " + what + " come along from " + sysName(x) + " (The Helmsman).", s);
                }
            for (int x : touched) removeExcessCapacity(p, x);
            removeExcessCapacity(p, s);
            checkNexus(s);
            return true;
        }
        case Ab::PromiseOfProtection: {
            const int m = playerOfFaction(G, "mentak");
            const int16_t note = makeNote(m, kNoteFaction);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), note);
            if (m < 0 || it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(note);
            abilityEvent2(E, p, "Promise of Protection", pn(p) + " plays Promise of Protection: " + pn(m) + " can't pillage them.");
            return true;
        }
        case Ab::TradeConvoys: {
            const int h = playerOfFaction(G, "hacan");
            const int16_t note = makeNote(h, kNoteFaction);
            auto it = std::find(pl.notes.begin(), pl.notes.end(), note);
            if (h < 0 || it == pl.notes.end()) return false;
            pl.notes.erase(it);
            pl.notesInPlay.push_back(note);
            abilityEvent2(E, p, "Trade Convoys",
                          pn(p) + " plays " + pn(h) + "'s Trade Convoys: they can now trade with players who aren't neighbours.");
            return true;
        }
        case Ab::StallTactics: {
            std::vector<Option> opts;
            for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
            opts.push_back({-1, "Cancel", ""});
            int c = fpick(p, Ab::StallTactics, "Stall Tactics: discard which action card?", opts, abilityText(pl.faction, 0));
            if (c < 0) return false;
            pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
            discardActionCard(p, c, false);
            pl.usedFactionAbility = true;
            abilityEvent2(E, p, "Stall Tactics", pn(p) + " stalls (Stall Tactics).");
            // Blackshade Infiltrator (DEPLOY)
            if (unitsAvailable(G, p, UnitType::Mech) > 0 && mechAbilities(G)) {
                std::vector<Option> where;
                for (int x : controlledPlanets(G, p))
                    if (!isDMZ(G, x) && !G.planets[x].destroyed) where.push_back({x, planetName(x), ""});
                if (!where.empty()) {
                    where.push_back({-1, "No mech", ""});
                    int x = fpick(p, Ab::YssarilMechDeploy, "Deploy a Blackshade Infiltrator mech on a planet you control?", where, "",
                                  false, true);
                    if (x >= 0) {
                        addUnits(p, UnitType::Mech, 1, G.planets[x].system, x);
                        say(p, pn(p) + " deploys a mech on " + planetName(x) + ".", G.planets[x].system);
                    }
                }
            }
            return true;
        }
        case Ab::MageonImplants: {
            std::vector<Option> who;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[q].eliminated && !G.players[q].actionCards.empty())
                    who.push_back({q, pn(q), std::to_string(G.players[q].actionCards.size()) + " action cards"});
            if (who.empty()) return false;
            who.push_back({-1, "Cancel", ""});
            int q = fpick(p, Ab::MageonImplants, "Mageon Implants: look at whose action cards?", who, techSummary(findTech("mi")));
            if (q < 0) return false;
            exhaustTech(p, "mi");
            E.revealed();
            std::vector<Option> opts;
            for (int16_t c : G.players[q].actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
            int c = fpick(p, Ab::MageonImplants, "Mageon Implants: take which card?", opts);
            moveCard(G.players[q], pl, c);
            abilityEvent2(E, p, "Mageon Implants", pn(p) + " takes an action card from " + pn(q) + " (Mageon Implants).");
            enforceHandLimit(p);
            return true;
        }
        case Ab::YinHero: {
            std::vector<int> picks;
            const int mec = G.mecatol();
            for (int i = 0; i < 3 && unitsAvailable(G, p, UnitType::Infantry) > int(picks.size()); ++i) {
                std::vector<Option> opts;
                for (size_t x = 0; x < G.planets.size(); ++x) {
                    const PlanetState& ps = G.planets[x];
                    // (fixes 26, the Yin review - owner 2026-11-12) "any non-home planets": planets the Yin control too, and Mallice
                    // (LRR 100.1b: the wormhole nexus is part of the game board)
                    const bool v26 = G.fixes >= 26;
                    if (ps.destroyed || ps.system < 0 || (ps.owner == p && !v26) || G.systems[ps.system].home >= 0 ||
                        (G.systems[ps.system].offBoard && !(v26 && ps.system == G.nexus)) || isDMZ(G, int(x)))
                        continue;
                    if (ps.system == mec && G.systems[mec].custodians && !G.custodiansRemoved) continue;
                    int def = 0;
                    for (int q = 0; q < G.numPlayers; ++q)
                        if (q != p) def += ps.units[q].groundForces();
                    int mine = int(std::count(picks.begin(), picks.end(), int(x)));
                    opts.push_back({int(x), planetName(int(x)) + (ps.owner >= 0 ? " (" + pn(ps.owner) + ")" : ""),
                                    std::to_string(def) + " defenders" + (mine ? ", " + std::to_string(mine) + " of yours chosen" : "")});
                }
                if (opts.empty()) break;
                opts.push_back({-1, picks.empty() ? std::string("Cancel") : "Done (" + std::to_string(picks.size()) + " chosen)", ""});
                int x = fpick(p, Ab::YinHero, "Dannel of the Tenth: land infantry where? (up to 3)", opts, leaderText(findLeader("yinhero")),
                              false, true);
                if (x < 0) break;
                picks.push_back(x);
            }
            if (picks.empty()) return false;
            purgeHero(p);
            heroEvent2(E, G, p, pn(p) + " uses Dannel of the Tenth.");
            dannelInvade(p, picks);
            checkEliminations();
            return true;
        }
        case Ab::YssarilHero: {
            if (!othersHaveCards(G, p)) return false;
            purgeHero(p);
            heroEvent2(E, G, p, pn(p) + " uses Kyver, Blade and Key.");
            for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
                if (q == p || G.players[q].actionCards.empty()) continue;
                std::vector<Option> opts;
                for (int16_t c : G.players[q].actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
                int c = opts.size() == 1 ? opts[0].id
                                         : fpick(q, Ab::YssarilHero, "Kyver: show " + pn(p) + " which action card?", opts,
                                                 "They take it, or make you discard 3 random action cards.");
                const int n = int(G.players[q].actionCards.size());
                int ch = fpick(p, Ab::YssarilHero, pn(q) + " shows you " + kActionCards[c].name,
                               {{1, "Take it", actionCardText(c)},
                                {0, "Make them discard 3 random action cards", pn(q) + " has " + std::to_string(n) + " action cards"}});
                if (ch == 1) {
                    moveCard(G.players[q], pl, c);
                    say(p, pn(p) + " takes " + pn(q) + "'s " + kActionCards[c].name + " (Kyver).");
                } else {
                    for (int i = 0; i < 3 && !G.players[q].actionCards.empty(); ++i) {
                        auto& h = G.players[q].actionCards;
                        int at = E.rng().below(int(h.size()));
                        int card = h[at];
                        h.erase(h.begin() + at);
                        discardActionCard(q, card, false);
                    }
                }
            }
            enforceHandLimit(p);
            return true;
        }
        default: return factionAction3(p, ab);
    }
}

}  // namespace ti
