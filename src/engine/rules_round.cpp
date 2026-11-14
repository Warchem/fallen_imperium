// Game setup and the round structure: strategy phase, action phase and
// status phase (the agenda phase is in rules_agenda.cpp).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "engine/breakthroughs.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

void Rules::playGame() {
    // Leaders unlock as soon as their condition is met: checked before every question.
    struct AskHook {
        Engine& e;
        ~AskHook() { e.beforeAsk = nullptr; }
    } hook{E};
    E.beforeAsk = [this] {
        checkUnlocks();
        fleetStateCheck();  // (0.46.1, fixes 15: the fleet pool at all times)
        sunderedCheck();    // (fixes 24: Sundered - other players' units in the Crimson home, however they got there)
        oceanCheck();       // (fixes 27: Oceanbound's limit, any time)
    };
    try {
        setupGame();
        for (;;) {
            ++G.round;
            E.log(-1, "--- Round " + std::to_string(G.round) + " ---", EventKind::Phase);
            strategyPhase();
            actionPhase();
            statusPhase();
            if (G.custodiansRemoved) agendaPhase();
            roundEndFactions();
            eventsRoundEnd();  // Dangerous Wilds (galactic event)
            promiseRoundEnd();
            persRoundEnd();  // betrayal timeouts, Chaotic picks, the round-3 reveal
            E.log(-1, "Round " + std::to_string(G.round) + " ends", EventKind::RoundEnd);
        }
    } catch (const GameOverSignal&) {
        G.phase = Phase::GameOver;
        G.activePlayer = -1;
        G.activeSystem = -1;
        if (G.winner >= 0)
            E.log(G.winner, pn(G.winner) + " wins the game with " + std::to_string(G.players[G.winner].vp) +
                                " victory points!",
                  EventKind::Score);
    }
}

// =================================================================== setup
void Rules::setupGame() {
    const Setup& s = E.setup();
    const int n = std::max(3, std::min(8, s.players));
    G.numPlayers = n;
    G.vpGoal = s.vpGoal;
    G.aiLevel = int8_t(std::max(0, std::min(3, s.aiLevel)));
    G.tradeRules = int8_t(s.trade & 15);
    G.extras = uint8_t(s.extras & 255);
    G.habits = s.habits;  // (0.54.0)
    G.modules = uint8_t(s.modules & 255);
    G.events = (G.modules & kModThundersEdge) ? uint32_t(s.events) : 0u;  // galactic events (0.36.0)
    G.dsLevel = int8_t((G.modules & kModDiscordant) ? s.dsLevel : 0);    // Discordant Stars batches (0.37.0)
    G.fixes = uint8_t(s.fixes & 255);
    G.layout = uint8_t(s.layout == 1 ? 1 : 0);  // (0.58.0)
    setStrategyTexts((G.modules & kModThundersEdge) != 0);  // 0.43.3: the strategy card wording of the cards in this game
    G.players.assign(n, Player());
    for (int i = 0; i < kStrategyCards; ++i) G.cardOwner[i] = -1;

    // Factions: take the requested ones, fill the rest at random.
    std::vector<int> chosen(n, -1), pool;
    std::vector<bool> used(kFactionCount, false);
    const int keleres = findFaction("keleres");
    for (int i = 0; i < n && i < int(s.factions.size()); ++i)
        if (s.factions[i] >= 0 && s.factions[i] < kFactionCount && !used[s.factions[i]]) {
            chosen[i] = s.factions[i];
            used[chosen[i]] = true;
        }
    // a Keleres seat that asked for a home (The Tribunii): that faction isn't dealt to anyone
    for (int i = 0; i < n && i < int(s.tribunii.size()); ++i)
        if (chosen[i] == keleres && s.tribunii[i] >= 0 && s.tribunii[i] < kFactionCount) used[s.tribunii[i]] = true;
    // (the Council Keleres joins the random pool only in games started with 0.21.0 or later:
    // older saves must deal the same factions as before)
    for (int f = 0; f < kFactionCount; ++f) {
        // Thunder's Edge factions (0.35.0) only with the module; the Obsidian is never dealt (the Firmament becomes it)
        if (kFactions[f].source == Source::ThundersEdge && (!(s.modules & kModThundersEdge) || !std::strcmp(kFactions[f].key, "obsidian")))
            continue;
        // Discordant Stars (0.37.0): with the module, and only the factions that are in
        if (kFactions[f].source == Source::Discordant && (!(s.modules & kModDiscordant) || !factionInPool(f, s.dsLevel))) continue;
        if (!used[f] && (f != keleres || (s.extras & 2))) pool.push_back(f);
    }
    E.rng().shuffle(pool);
    for (int i = 0; i < n; ++i)
        if (chosen[i] < 0) {
            chosen[i] = pool.back();
            pool.pop_back();
        }
    // The Tribunii: the Keleres take the home system of the Mentak, the Xxcha or the Argent
    // Flight - one not in the game. With all three in play the Keleres can't be played.
    std::vector<int> tribunii(n, -1), homeFaction = chosen;
    for (int i = 0; i < n; ++i) {
        if (chosen[i] != keleres) continue;
        std::vector<int> free;
        for (const char* k : {"mentak", "xxcha", "argent"}) {
            const int f = findFaction(k);
            if (std::find(chosen.begin(), chosen.end(), f) == chosen.end()) free.push_back(f);
        }
        if (free.empty()) {  // swap the Keleres for another faction
            chosen[i] = homeFaction[i] = pool.back();
            pool.pop_back();
            continue;
        }
        int t = i < int(s.tribunii.size()) ? s.tribunii[i] : -1;
        if (std::find(free.begin(), free.end(), t) == free.end()) t = free[E.rng().below(int(free.size()))];
        tribunii[i] = homeFaction[i] = t;
    }
    // Colours: the seats' picks (faction menu, 0.19.0), then the human's choice;
    // everyone else takes the next free colour in seat order.
    std::vector<int> col(n, -1);
    std::vector<bool> taken(8, false);
    for (int i = 0; i < n && i < int(s.colors.size()); ++i)
        if (s.colors[i] >= 0 && s.colors[i] < 8 && !taken[s.colors[i]]) {
            col[i] = s.colors[i];
            taken[col[i]] = true;
        }
    if (s.human >= 0 && s.human < n && col[s.human] < 0 && s.humanColor >= 0 && s.humanColor < 8 && !taken[s.humanColor]) {
        col[s.human] = s.humanColor;
        taken[s.humanColor] = true;
    }
    for (int i = 0, next = 0; i < n; ++i) {
        if (col[i] >= 0) continue;
        while (next < 8 && taken[next]) ++next;
        col[i] = next < 8 ? next : 0;
        if (next < 8) taken[next] = true;
    }
    for (int i = 0; i < n; ++i) {
        Player& p = G.players[i];
        p.faction = int8_t(chosen[i]);
        p.tribunii = int8_t(tribunii[i]);
        p.color = int8_t(col[i]);
        p.human = s.isHuman(i) && !s.autoplay;  // watching: the computer plays this seat like any other
        p.name = factionShortName(chosen[i]);
        if (i < int(s.levels.size()) && s.levels[i] >= 0) p.aiLevel = int8_t(std::min(3, s.levels[i]));
    }
    setupPersonalities();  // computer players' archetypes, traits and B-Factors (0.12.0)

    std::string err;
    if (s.draft) {  // 0.15.0: the speaker first, then the players build the galaxy
        G.speaker = int8_t(E.rng().below(n));
        galaxyDraft(homeFaction);
    } else {
        if (s.mapSeed) {  // 0.43.0: the seeded galaxy - the map from the player's number, balanced harder
            Rng mr(uint32_t(s.mapSeed * 2654435761u + uint32_t(n)));  // (the New Game slice preview seeds it the same way)
            if (!buildGalaxy(G, mr, homeFaction, &err, true)) throw std::runtime_error(err);
        } else if (!buildGalaxy(G, E.rng(), homeFaction, &err)) {
            throw std::runtime_error(err);
        }
        G.speaker = int8_t(E.rng().below(n));
    }

    for (int p = 0; p < n; ++p) setupFaction(p);
    for (int p = 0; p < n; ++p)  // 0.58.0: the printed five-player board without hyperlanes - trade goods for the seats
        if (const int tg = layoutStartTg(G, p)) {  // placed worse (LRR setup step 6)
            G.players[p].tradeGoods = int8_t(G.players[p].tradeGoods + tg);
            E.log(p, pn(p) + " starts with " + std::to_string(tg) + " trade goods (home system's place on the board).");
        }
    if (G.modules & kModThundersEdge) tePlacement(G);  // where the Fracture and Thunder's Edge go (0.34.0)

    // Objectives: 5 stage I and 5 stage II face down; reveal 2 stage I.
    std::vector<int16_t> s1, s2, sec;
    for (int o = 0; o < kObjectiveCount; ++o) {
        if (!objectiveImplemented(o)) continue;
        if (kObjectives[o].kind == 1) s1.push_back(int16_t(o));
        else if (kObjectives[o].kind == 2) s2.push_back(int16_t(o));
        else sec.push_back(int16_t(o));
    }
    E.rng().shuffle(s1);
    E.rng().shuffle(s2);
    E.rng().shuffle(sec);
    G.stage1Rest.assign(s1.begin() + 5, s1.end());  // set aside (Incentive Program)
    G.stage2Rest.assign(s2.begin() + 5, s2.end());
    s1.resize(5);
    s2.resize(5);
    G.stage1Deck = s1;
    G.stage2Deck = s2;
    G.secretDeck = sec;

    // Action card and agenda decks (base + Prophecy of Kings), promissory notes.
    for (int c = 0; c < kActionCardCount; ++c) {
        if (kActionCards[c].source == Source::Uncharted && !(G.modules & kModUncharted)) continue;  // (0.33.0)
        if (kActionCards[c].source == Source::ThundersEdge && !(G.modules & kModThundersEdge)) continue;  // (0.34.0)
        if (actionCardImplemented(c))
            for (int k = 0; k < kActionCards[c].count; ++k) G.actionDeck.push_back(int16_t(c));
    }
    E.rng().shuffle(G.actionDeck);
    for (int a = 0; a < kAgendaCount; ++a)
        if (agendaImplemented(a)) G.agendaDeck.push_back(int16_t(a));
    E.rng().shuffle(G.agendaDeck);
    for (int p = 0; p < n; ++p)
        for (int t = 0; t < kNoteTypes; ++t) G.players[p].notes.push_back(makeNote(p, t));
    for (int p = 0; p < n; ++p) factionSetup(p);  // leaders, Alliance, faction notes
    for (int p = 0; p < n; ++p) keleresStartingTechs(p);  // after everyone else has theirs (and their leaders, notes)
    setupExploration();
    E.log(-1, "The galaxy is set. " + pn(G.speaker) + " is the speaker.", EventKind::Phase);
    revealObjective();
    revealObjective();

    // Secret objectives: each player draws 2 and keeps 1.
    for (int p : fromSpeaker(G)) {
        if (G.secretDeck.size() < 2) break;
        int a = G.secretDeck.back();
        G.secretDeck.pop_back();
        int b = G.secretDeck.back();
        G.secretDeck.pop_back();
        E.revealed();
        int keep = pick(p, Tag::SetupSecret, "Keep which secret objective?",
                        {{a, kObjectives[a].name, objectiveSummary(a)}, {b, kObjectives[b].name, objectiveSummary(b)}},
                        "The other one goes back into the deck.");
        G.players[p].secrets.push_back(int16_t(keep));
        G.secretDeck.push_back(int16_t(keep == a ? b : a));
    }
    E.rng().shuffle(G.secretDeck);
    setupEvents();  // Thunder's Edge galactic events (0.36.0)
}

// Galaxy setup (LRR setup step 6): each player holds 3 blue and 2 red tiles;
// starting with the speaker and going clockwise each places one, the last
// player places two in a row and the order snakes back - until all are placed.
void Rules::galaxyDraft(const std::vector<int>& factionBySeat) {
    std::string err;
    if (!startGalaxyDraft(G, E.rng(), factionBySeat, &err)) throw std::runtime_error(err);
    const std::vector<int> fwd = fromSpeaker(G);
    size_t per = 0;
    for (const auto& h : G.draft.hands) per = std::max(per, h.size());
    E.log(-1, "Galaxy setup: everyone places " + std::to_string(per) + " system tiles, " + pn(G.speaker) +
                  " (the speaker) first, then clockwise and back.",
          EventKind::Phase);
    std::vector<int> order;
    for (size_t pass = 0; pass < per; ++pass) {
        if (pass % 2 == 0) order.insert(order.end(), fwd.begin(), fwd.end());
        else order.insert(order.end(), fwd.rbegin(), fwd.rend());
    }
    for (int p : order) {
        if (G.draft.hands[size_t(p)].empty()) continue;
        G.draft.placing = int8_t(p);
        std::vector<Option> opts;
        int ring = 0;
        for (const auto& pr : draftPlacements(G, p)) {
            const SystemDef& d = kSystems[pr.first];
            ring = G.draft.cells[size_t(pr.second)].ring;
            opts.push_back({pr.first * 1000 + pr.second,
                            d.planetCount ? std::string(d.name) : std::string(d.name) + " (" + d.key + ")",
                            "ring " + std::to_string(ring)});
        }
        if (opts.empty()) break;  // (can't happen: there are as many spaces as tiles)
        const int left = int(G.draft.hands[size_t(p)].size());
        const int id = pick(p, Tag::GalaxyPlace, "Galaxy setup: place a system tile",
                            opts, "Ring " + std::to_string(ring) + " around Mecatol Rex. " + std::to_string(left) +
                                      (left == 1 ? " tile" : " tiles") + " left in your hand.");
        const int def = id / 1000, cell = id % 1000;
        const int sys = placeDraftTile(G, p, def, cell);
        const SystemDef& d = kSystems[def];
        event(EventKind::TilePlaced, p,
              pn(p) + " places " + (d.planetCount ? std::string(d.name) : std::string(d.name) + " (" + d.key + ")") + " in ring " +
                  std::to_string(ring) + ".",
              sys);
    }
    finishGalaxyDraft(G);
}

void Rules::setupFaction(int p) {
    Player& pl = G.players[p];
    const FactionDef& f = kFactions[pl.faction];
    for (int t = 0; t < kUnitTypes; ++t) pl.unit[t] = int16_t(genericUnit(UnitType(t)));
    for (int u : f.units) {
        if (u < 0) continue;
        if (kUnits[u].upgradeOf < 0) pl.unit[int(kUnits[u].type)] = int16_t(u);
    }
    secondFlagship(p);  // (0.52.0: the Ghemina's Lady)
    for (size_t i = 0; i < G.systems.size(); ++i)
        if (G.systems[i].home == p) pl.homeSystem = int16_t(i);
    SystemState& home = G.systems[pl.homeSystem];
    for (int16_t x : home.planets) {
        G.planets[x].owner = int8_t(p);
        G.planets[x].exhausted = false;
    }
    // The Keleres put their ground forces and space dock where the faction whose home they took has its dock.
    int keleresSlot = -1;
    if (pl.tribunii >= 0)
        for (int i = 0; i < kFactions[pl.tribunii].fleetCount; ++i)
            if (kFactions[pl.tribunii].fleet[i].type == UnitType::SpaceDock) keleresSlot = kFactions[pl.tribunii].fleet[i].planet;
    for (int i = 0; i < f.fleetCount; ++i) {
        const StartUnit& su = f.fleet[i];
        int planet = -1;
        bool space = su.planet < 0 || (su.type == UnitType::SpaceDock && pl.u(UnitType::SpaceDock).spaceOnly);
        const int slot = su.planet >= 0 && keleresSlot >= 0 ? keleresSlot : su.planet;
        if (!space && slot < int(home.planets.size())) planet = home.planets[slot];
        addUnits(p, su.type, su.count, pl.homeSystem, space ? -1 : planet);
    }
    for (int t : f.startTechs)
        if (t >= 0) researchTech(p, t);
    // The Nekro Virus can't research: it starts with both Valefar Assimilators
    if (!std::strcmp(f.key, "nekro") && factionImplemented(pl.faction)) {
        researchTech(p, findTech("vax"));
        researchTech(p, findTech("vay"));
    }

    // Factions that choose their starting technologies.
    if (!std::strcmp(f.key, "winnu")) {
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t) {
            const TechDef& d = kTechs[t];
            if (d.faction >= 0 || d.unitUpgrade || d.color == TechColor::None) continue;
            if (d.req[0] + d.req[1] + d.req[2] + d.req[3]) continue;
            opts.push_back({t, d.name, techSummary(t)});
        }
        researchTech(p, pick(p, Tag::SetupTech, "Winnu: choose a starting technology", opts,
                             "Any technology that has no prerequisites."));
    } else if (!std::strcmp(f.key, "argent")) {
        std::vector<Option> opts;
        for (const char* k : {"nm", "st", "ps"}) {
            int t = findTech(k);
            opts.push_back({t, kTechs[t].name, techSummary(t)});
        }
        for (int t : pickMany(p, Tag::SetupTech, "Argent: choose 2 starting technologies", opts, 2, 2))
            researchTech(p, t);
    } else if (f.source == Source::Discordant) {
        dsStartingTechs(p);  // Discordant Stars (rules_ds.cpp)
    } else if (f.source == Source::ThundersEdge && G.fixes >= 2) {
        // 0.43.0 (owner: "Last Bastion selection of initial tech didn't trigger" - none of these were offered)
        struct TeStart {
            const char* faction;
            const char* techs[4];
        };
        static const TeStart kTeStart[] = {{"bastion", {"amd", "det", "st", "sdn"}},
                                           {"firmament", {"nm", "pa", "st", "sdn"}},
                                           {"crimson", {"det", "amd", "ps", "aida"}},
                                           {"ralnel", {"nm", "pa", "ps", "aida"}}};
        for (const TeStart& ts : kTeStart) {
            if (std::strcmp(f.key, ts.faction) != 0) continue;
            std::vector<Option> opts;
            for (const char* k : ts.techs) {
                const int t = findTech(k);
                if (t >= 0 && !pl.hasTech(t)) opts.push_back({t, kTechs[t].name, techSummary(t)});
            }
            if (!opts.empty())
                researchTech(p, pick(p, Tag::SetupTech, std::string(factionShortName(pl.faction)) + ": choose a starting technology", opts));
        }
        if (!std::strcmp(f.key, "deepwrought"))  // research 2 technologies during setup (prerequisites apply)
            for (int k = 0; k < 2; ++k) {
                std::vector<Option> opts;
                for (int t = 0; t < kTechCount; ++t) {
                    const TechDef& d = kTechs[t];
                    if (d.faction >= 0 || d.color == TechColor::None || pl.hasTech(t) || !techImplemented(t)) continue;
                    if (d.source != Source::Base && d.source != Source::PoK) continue;
                    int miss[4];
                    missingPrereqs(G, p, t, miss);
                    if (miss[0] + miss[1] + miss[2] + miss[3] > 0) continue;
                    opts.push_back({t, d.name, techSummary(t)});
                }
                if (opts.empty()) break;
                researchTech(p, pick(p, Tag::SetupTech, "Deepwrought: research a starting technology (" + std::to_string(k + 1) + " of 2)",
                                     opts, "Prerequisites apply: the second may build on the first."));
            }
    }
    pl.tactic = 3;
    pl.fleet = 3;
    pl.strategy = 2;
}

// The Council Keleres start with 2 non-faction technologies of their choice
// among those the other players own (only 1 if the others own just one).
void Rules::keleresStartingTechs(int p) {
    if (!fac(p, "keleres")) return;
    std::vector<Option> opts;
    std::vector<int> seen;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p) continue;
        for (int16_t t : G.players[q].techs) {
            if (kTechs[t].faction >= 0 || G.players[p].hasTech(t)) continue;
            if (std::find(seen.begin(), seen.end(), int(t)) != seen.end()) continue;
            seen.push_back(t);
            opts.push_back({t, kTechs[t].name, techSummary(t)});
        }
    }
    if (opts.empty()) return;
    const int k = std::min(2, int(opts.size()));
    for (int t : pickMany(p, Tag::SetupTech, k == 2 ? "Keleres: choose 2 starting technologies" : "Keleres: choose a starting technology",
                          opts, k, k, "From the technologies the other players start with."))
        researchTech(p, t);
}

void Rules::revealObjective() {
    std::vector<int16_t>& deck = G.stage1Revealed < 5 ? G.stage1Deck : G.stage2Deck;
    if (deck.empty()) {
        // No objectives left to reveal: the game ends now.
        int best = -1;
        for (int p : initiativeOrder(G))
            if (best < 0 || G.players[p].vp > G.players[best].vp) best = p;
        E.log(-1, "No public objectives are left to reveal: the game ends.", EventKind::Phase);
        G.winner = int8_t(best);
        throw GameOverSignal();
    }
    int o = deck.front();
    deck.erase(deck.begin());
    E.revealed();
    if (kObjectives[o].kind == 1) ++G.stage1Revealed;
    G.publicObjectives.push_back(int16_t(o));
    E.log(-1, std::string("New public objective: ") + kObjectives[o].name + " (" + std::to_string(kObjectives[o].points) +
                  " VP) - " + objectiveSummary(o),
          EventKind::Phase);
    Event e;  // 0.29.0 (owner): the new objective opens large (display only)
    e.kind = EventKind::ObjectiveRevealed;
    e.count = int16_t(o);
    E.emit(std::move(e));
    neuraloop(o);  // Thunder's Edge relic (0.34.0)
    if (G.modules & kModDiscordant) dsObjectiveRevealed();  // Oracle AI (Augurs)
}

// =================================================================== strategy phase
void Rules::strategyPhase() {
    G.phase = Phase::Strategy;
    E.log(-1, "Strategy phase", EventKind::Phase);
    G.lawsBlank = false;
    applyPendingEffects();
    councilPatronage();  // Keleres: at the start of the strategy phase
    for (Player& p : G.players) {
        if (!p.keepsCards) {
            p.cards.clear();
            p.cardsUsed = 0;
        }
        p.passed = p.eliminated;
    }
    for (int c = 0; c < kStrategyCards; ++c) G.cardOwner[c] = -1;
    for (int q = 0; q < G.numPlayers; ++q)
        for (int8_t c : G.players[q].cards) G.cardOwner[c] = int8_t(q);
    {
        WinCtx c;
        c.w = Win::StrategyStart;
        window(c);
    }
    strategyStart4();  // Scepter of Dominion
    if (G.modules & kModDiscordant) dsStrategyStart();  // Vector Programs (Kyro)
    const int picksEach = G.numPlayers <= 4 ? 2 : 1;
    const bool checks = lawInPlay(G, "checks");
    for (int round = 0; round < picksEach; ++round) {
        for (int p : fromSpeaker(G)) {
            if (G.players[p].eliminated || G.players[p].keepsCards) continue;
            std::vector<int> banned;
            int c = -1;
            for (int attempt = 0; attempt < 8; ++attempt) {
                std::vector<Option> opts;
                for (int k = 0; k < kStrategyCards; ++k) {
                    if (G.cardOwner[k] >= 0 || std::find(banned.begin(), banned.end(), k) != banned.end()) continue;
                    std::string label = std::to_string(k + 1) + " " + strategyName(k);
                    if (G.cardTradeGoods[k]) label += " (+" + std::to_string(G.cardTradeGoods[k]) + " TG)";
                    opts.push_back({k, label, strategyPrimary(k)});
                }
                if (opts.empty()) break;
                c = pick(p, Tag::StrategyPick, "Choose a strategy card", opts,
                         checks ? "Checks and Balances: you will give it to another player." : "");
                // Public Disgrace: the player must choose a different card, if able.
                WinCtx w;
                w.w = Win::PickStrategy;
                w.a = p;
                w.value = c;
                if (cardHas(window(w), "disgrace") && opts.size() > 1) {
                    banned.push_back(c);
                    c = -1;
                    continue;
                }
                break;
            }
            if (c < 0) continue;
            int receiver = p;
            if (checks) {
                // Checks and Balances: give the card to a player with fewer cards.
                std::vector<Option> who;
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p && !G.players[q].eliminated && int(G.players[q].cards.size()) < picksEach &&
                        !G.players[q].keepsCards)
                        who.push_back({q, pn(q), std::to_string(G.players[q].cards.size()) + " strategy cards"});
                if (!who.empty())
                    receiver = who.size() == 1 ? who[0].id
                                               : pick(p, Tag::StrategyGive, std::string("Checks and Balances: give ") +
                                                                                strategyName(c) + " to whom?",
                                                      who);
            }
            G.cardOwner[c] = int8_t(receiver);
            G.players[receiver].cards.push_back(int8_t(c));
            std::string extra;
            if (G.cardTradeGoods[c] && c == G.blight && G.blightOwner >= 0 && G.blightOwner != receiver) {
                // Brikk Strain (Kyro hero): the trade goods on that card go to the Kyro
                gainTG(G.blightOwner, G.cardTradeGoods[c]);
                say(G.blightOwner, pn(G.blightOwner) + " takes the " + std::to_string(G.cardTradeGoods[c]) + " trade goods on " + strategyName(c) + " (Brikk Strain).");
                G.cardTradeGoods[c] = 0;
            }
            if (G.cardTradeGoods[c]) {
                gainTG(receiver, G.cardTradeGoods[c]);
                extra = " and its " + std::to_string(G.cardTradeGoods[c]) + " trade good(s)";
                G.cardTradeGoods[c] = 0;
            }
            Event ev;
            ev.kind = EventKind::StrategyPick;
            ev.player = int8_t(receiver);
            ev.card = int16_t(c);
            ev.text = receiver == p ? pn(p) + " takes " + strategyName(c) + extra + "."
                                    : pn(p) + " picks " + strategyName(c) + " and gives it to " + pn(receiver) + extra + ".";
            E.emit(ev);
        }
    }
    for (int c = 0; c < kStrategyCards; ++c)
        if (G.cardOwner[c] < 0) ++G.cardTradeGoods[c];
    for (Player& p : G.players) p.keepsCards = false;

    // Imperial Arbiter: its owner may swap a strategy card with another player.
    int arb = lawOwner(G, "arbiter");
    if (arb >= 0 && !G.players[arb].eliminated && !G.players[arb].cards.empty()) {
        std::vector<Option> opts;
        for (int8_t mine : G.players[arb].cards)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != arb)
                    for (int8_t theirs : G.players[q].cards)
                        opts.push_back({mine * 100 + theirs, std::string("Swap your ") + strategyName(mine) + " for " + pn(q) +
                                                                  "'s " + strategyName(theirs),
                                        ""});
        if (!opts.empty()) {
            opts.push_back({-1, "Keep Imperial Arbiter", ""});
            int c = pick(arb, Tag::LawAbility, "Discard Imperial Arbiter to swap strategy cards?", opts);
            if (c >= 0) {
                int mine = c / 100, theirs = c % 100, q = G.cardOwner[theirs];
                auto& a = G.players[arb].cards;
                auto& b = G.players[q].cards;
                *std::find(a.begin(), a.end(), int8_t(mine)) = int8_t(theirs);
                *std::find(b.begin(), b.end(), int8_t(theirs)) = int8_t(mine);
                G.cardOwner[mine] = int8_t(q);
                G.cardOwner[theirs] = int8_t(arb);
                say(arb, pn(arb) + " swaps " + strategyName(mine) + " for " + pn(q) + "'s " + strategyName(theirs) + ".");
                for (size_t i = 0; i < G.laws.size(); ++i)
                    if (!std::strcmp(kAgendas[G.laws[i].agenda].key, "arbiter")) {
                        discardLaw(int(i), "used");
                        break;
                    }
            }
        }
    }
    telepathic();          // Naalu "0" token / Gift of Prescience
    endOfStrategyPhase();  // Quantum Datahub Node
}

// =================================================================== action phase
void Rules::actionPhase() {
    G.phase = Phase::Action;
    E.log(-1, "Action phase", EventKind::Phase);
    for (Player& p : G.players) p.passed = p.eliminated;
    std::vector<int> order = initiativeOrder(G);
    for (;;) {
        bool anyone = false;
        for (int p : order) {
            if (G.players[p].passed) continue;
            anyone = true;
            if (skipNext_) {  // Crisis (Thunder's Edge): this turn is skipped
                skipNext_ = false;
                say(p, pn(p) + "'s turn is skipped (Crisis).");
                continue;
            }
            G.activePlayer = int8_t(p);
            E.log(p, pn(p) + "'s turn", EventKind::TurnStart);
            lastActiveSystem_ = -1;
            lawsOrder(p);  // Keleres: blank all laws this turn
            startOfTurn(p);
            teTurnStart(p);  // Extreme Duress (Thunder's Edge)
            if (G.modules & kModDiscordant) dsTurnStart(p);  // Troubadour, Spoils of War (Discordant Stars)
            for (int q = 0; q < G.numPlayers; ++q) btTurnStart(q);  // Thunder's Paradox (Nomad breakthrough)
            turnEnded_ = false;
            bool acted = false;
            if (dsEndTurnNow_) {  // Troubadour (Lanefir mech, 0.43.0): the turn ends at once - no action, not passed
                dsEndTurnNow_ = false;
                ++G.players[p].turns;
                ++G.turnCount;
                say(p, pn(p) + " ends its turn (Troubadour).");
            } else {
                acted = takeTurn(p, false);
                if (G.fixes >= 11 && (G.modules & kModDiscordant)) pilgrimage(p);  // (0.44.7: Pilgrimage Beacons)
            }
            bool fleetLogistics = owns(p, "fl");
            for (int extra = 0; extra < 6 && acted && !turnEnded_ && !G.players[p].passed && !G.players[p].eliminated; ++extra) {
                // Fleet Logistics, Master Plan, Minister of War: another action.
                bool more = false;
                if (suffiAnExtra(p)) {  // Suffi An (Keleres commander): after a component action
                    more = true;
                } else if (fleetLogistics) {
                    fleetLogistics = false;
                    more = true;
                } else {
                    WinCtx w;
                    w.w = Win::AfterAction;
                    w.active = p;
                    if (cardHas(window(w), "master_plan", p)) more = true;
                    if (!more && lawOwner(G, "minister_war") == p && tokensOnBoard(p) > 0) {
                        std::vector<Option> opts;
                        for (size_t sy = 0; sy < G.systems.size(); ++sy)
                            if (G.systems[sy].hasToken(p)) opts.push_back({int(sy), sysName(int(sy)), ""});
                        opts.push_back({-1, "Keep Minister of War", ""});
                        int sy = pick(p, Tag::LawAbility, "Discard Minister of War: return a command token and take another action?", opts,
                                      "", true);
                        if (sy >= 0) {
                            G.systems[sy].tokens &= uint16_t(~(1u << p));
                            for (size_t i = 0; i < G.laws.size(); ++i)
                                if (!std::strcmp(kAgendas[G.laws[i].agenda].key, "minister_war")) {
                                    discardLaw(int(i), "used");
                                    break;
                                }
                            more = true;
                        }
                    }
                }
                if (!more) break;
                acted = takeTurn(p, true, true);
                if (G.fixes >= 11 && (G.modules & kModDiscordant)) pilgrimage(p);
            }
            if (!G.players[p].eliminated) endOfTurn(p);
            if (G.modules & kModThundersEdge) coexistStructures();  // (fixes 25: LRR 63.4 / 79.4 - whatever the turn did)
            G.lawsBlank = false;  // Law's Order lasts until the end of the turn
            if (!G.players[p].eliminated)
                E.log(p, G.players[p].passed ? pn(p) + " passes" : pn(p) + " ends turn", EventKind::TurnEnd);
            if (!G.players[p].eliminated) teTurnEnd(p);  // Puppets on a String, Crisis (Thunder's Edge)
            G.activePlayer = -1;
        }
        if (!anyone) {
            if ((G.modules & kModThundersEdge) && directorNel()) continue;  // Ral Nel hero: un-passed
            break;
        }
    }
    for (int16_t c : G.skimmer) G.actionDiscard.push_back(c);  // (Data Skimmer: nobody passed to claim them)
    G.skimmer.clear();
}

bool Rules::takeTurn(int p, bool second, bool mayPass) {
    Player& pl = G.players[p];
    std::vector<int> cancelled;  // options a computer player backed out of: not offered again this turn
    if (!second) {  // a new turn: new offer / transaction limits (LRR 94)
        resetDealCounters();
        sleeperAsked_ = 0;
        ++pl.turns;
        ++G.turnCount;
        persTurnStart(p);  // B-Factor rolls, betrayals
    }
    for (;;) {
        asnUsed_ = false;  // Agency Supply Network: once per action
        dsOncePerAction_ = 0;  // Rally to the Cause, Diplomats (Discordant Stars)
        lastComponent_ = false;
        std::vector<Option> opts;
        bool unusedCard = false;
        for (int8_t c : pl.cards)
            if (!((pl.cardsUsed >> c) & 1)) unusedCard = true;
        if (pl.tactic > 0)
            opts.push_back({0, "Tactical action", "Activate a system: move, fight, invade and produce."});
        for (int8_t c : pl.cards)
            if (!((pl.cardsUsed >> c) & 1))
                opts.push_back({10 + c, std::string("Strategic action: ") + strategyName(c), strategyPrimary(c)});
        for (const Option& o : componentActionOptions(p)) opts.push_back(o);
        for (const Option& o : factionActionOptions(p, false)) opts.push_back(o);
        // Psychoarchaeology: exhaust specialty planets for trade goods (doesn't use your action)
        if (owns(p, "pa")) {
            bool any = false;
            for (int x : ti::readyPlanets(G, p))
                if (planetHasSkip(G, x)) any = true;
            if (any) opts.push_back({600, "Psychoarchaeology", "Exhaust planets with technology specialties for 1 trade good each. Doesn't use your action."});
        }
        if (absoluteSynergyReady(p)) {  // (fixes 27, the Naaz-Rokha review) "When you have 4 mechs in the same system"
            const BreakthroughDef* bt = breakthroughFor(G, p);
            opts.push_back({645, "Breakthrough: Absolute Synergy", std::string(bt ? bt->text : "") + " Doesn't use your action."});
        }
        opts.erase(std::remove_if(opts.begin(), opts.end(),
                                  [&](const Option& o) {
                                      return std::find(cancelled.begin(), cancelled.end(), o.id) != cancelled.end();
                                  }),
                   opts.end());
        if (canNegotiate(p)) opts.push_back({650, "Negotiate", "Trade with neighbours or make promises. Doesn't use your action."});
        if (pl.commodities > 0) {  // Thunder's Edge: a ready space station turns commodities into trade goods
            bool station = false;
            for (int x : ti::readyPlanets(G, p))
                if (stationFor(G, p, x)) station = true;
            if (forgeworldsOf(G, p, true) > 0) station = true;  // (0.44.9: an undamaged Omni-Forgeworld)
            if (station)
                opts.push_back({641, "Space station: convert commodities",
                                "Exhaust a space station to turn your " + std::to_string(pl.commodities) +
                                    " commodities into trade goods. Doesn't use your action."});
        }
        if (!sleeperAskSpots(p).empty())  // (0.44.9: Slumberstate Computing - the owner's answer B9)
            opts.push_back({644, "Ask for a sleeper", "Slumberstate Computing: another player may let you place a sleeper token on "
                                                      "a planet of theirs. Doesn't use your action."});
        if (relicReady(p, "e6-g0_network"))  // Uncharted Space relic: at any time
            opts.push_back({640, "Relic: E6-G0 Network", "Exhaust it to draw 1 action card. Doesn't use your action."});
        // Local Contracts (Nokar, 0.43.0 D111): "during the action phase" - not a component action, so it doesn't use
        // the turn's action
        if ((G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtReady(p, "lanefir")) {  // Erasure Corps (0.43.0)
            bool any = false;
            for (int x : controlledPlanets(G, p))
                any = any || (!G.planets[size_t(x)].destroyed && kPlanets[G.planets[size_t(x)].def].trait != Trait::None);
            if (any) opts.push_back({643, "Breakthrough: Erasure Corps", "Exhaust it to explore one of your planets. Doesn't use your action."});
        }
        if (G.fixes >= 2 && techReady(p, "dsnokay") && (reinforcementTokens(p) > 0 || G.fixes >= 9)) {
            bool any = false;  // (0.44.5, fixes 9: a token from a pool when reinforcements are empty; systems with our token too)
            for (size_t s = 0; s < G.systems.size() && !any; ++s)
                any = (G.fixes >= 9 || !G.systems[s].hasToken(p)) && productionCapacity(G, p, int(s)) > 0;
            if (any)
                opts.push_back({642, "Local Contracts", "Exhaust it: produce with one of your units, then a command token from your "
                                                        "reinforcements goes in its system. Doesn't use your action."});
        }
        if (second && G.fixes >= 15) {
            // (0.46.1 - owner, RULES-CHECK Q1: passing may be the extra action too - told apart from ending the turn)
            opts.push_back({98, "End turn", "Skip the extra action. You stay in the round and take more turns later."});
            if (mayPass && !unusedCard)
                opts.push_back({99, "Pass (as the extra action)", "You are done for the round: no more turns until the next one."});
        } else if (second) {
            opts.push_back({98, "End turn", "You may take another action now; you may also skip it."});
        } else if (!unusedCard) {
            opts.push_back({99, "Pass", "You take no more turns this round."});
        }
        if (opts.empty()) {
            opts.push_back({99, "Pass", "No actions left."});
        }
        const std::string duressNote = !second && pl.duress >= 0
                                           ? "Extreme Duress (" + pn(pl.duress) + "): unless you make a strategic action, you discard your "
                                             "action cards, give " + pn(pl.duress) + " your trade goods and show your secret objectives."
                                           : "";
        int choice = (opts.size() == 1 && opts[0].id == 99) ? 99
                                                             : pick(p, Tag::ActionChoice, second ? "Take a second action?" : "Choose your action",
                                                                    opts, duressNote);
        if (choice == 98) return false;
        if (choice == 600) {
            psychoarchaeology(p);
            continue;
        }
        if (choice == 650) {
            negotiate(p, -1);
            continue;
        }
        if (choice == 640) {
            e6g0(p);
            continue;
        }
        if (choice == 641) {
            stationConvert(p);
            continue;
        }
        if (choice == 642) {
            if (!ds5Action(p, Ab::DsLocalContracts) && !pl.human) cancelled.push_back(choice);  // (not offered again this turn)
            continue;
        }
        if (choice == 643) {
            if (!dsbtLanefirExplore(p) && !pl.human) cancelled.push_back(choice);
            continue;
        }
        if (choice == 645) {  // Absolute Synergy (fixes 27)
            if (!btAction(p, int(Ab::BtNaaz)) && !pl.human) cancelled.push_back(choice);
            continue;
        }
        if (choice == 644) {
            if (!sleeperAsk(p) && !pl.human) cancelled.push_back(choice);
            continue;
        }
        if (choice >= 700 && choice < 1000) {  // faction options that don't use the action (Hacan agent)
            if (!factionAction(p, choice) && !pl.human) cancelled.push_back(choice);
            continue;
        }
        auto announce = [&](int8_t what, const std::string& text) {
            Event ev;
            ev.kind = EventKind::ActionChosen;
            ev.player = int8_t(p);
            ev.card = what;
            ev.text = text;
            E.emit(ev);
        };
        if (choice == 99) {
            pl.passed = true;
            announce(kActionPass, pn(p) + " passes for the rest of the round.");
            passWindow(p);  // Uncharted Space: AI Augury, Arbitrage (0.33.0)
            if (G.modules & kModDiscordant) dsPassed(p);  // Deliberate Action, Applied Biothermics
            tePass(p);      // Thunder's Edge legendary planets (0.34.0)
            skimmerPass(p);  // Data Skimmer (Ral Nel breakthrough)
            bool allPassed = true;
            for (const Player& q : G.players)
                if (!q.passed) allPassed = false;
            if (allPassed) actionSecret(p, "pe");
            if (allPassed) G.lastPasser = int8_t(p);
            return false;
        }
        if (!second && (choice == 0 || choice >= 1000) && pl.duress >= 0) duressCheck(p, false);  // Extreme Duress
        if (choice == 0) {
            int before = pl.tactic;
            tacticalAction(p);
            // cancelled before activating? 0.43.3 (tester, fixes >= 4): a command token gained during the action
            // (N'orr Supremacy, Volatile Fuel Source...) put the tactic pool back where it was, so it looked like
            // one - and the player got another action
            if (G.fixes >= 4 ? !activated_ : pl.tactic == before) continue;
            return true;
        }
        if (choice >= 1000) {
            // Shown large (owner, 0.16.0): technologies, fragments, the Enigmatic Device and
            // faction component actions. Action cards and relics have their own events.
            if (choice >= 2000 && !(choice >= 4000 && choice < 5000))
                for (const Option& o : opts)
                    if (o.id == choice) {
                        Event ev;
                        ev.kind = EventKind::ComponentUsed;
                        ev.player = int8_t(p);
                        ev.count = int16_t(choice);
                        ev.label = o.label + "\n" + o.detail;  // name, then what it does
                        ev.text = pn(p) + " uses " + o.label + ".";
                        E.emit(ev);
                        break;
                    }
            if (!componentAction(p, choice)) {  // cancelled: the action isn't used (LRR 22.4)
                if (!pl.human) cancelled.push_back(choice);  // computer players don't pick it again this turn
                continue;
            }
            lastComponent_ = true;  // Suffi An (Keleres commander)
            if (G.modules & kModDiscordant) dsAfterComponent(p);  // Coil Overcharge (Kolume hero)
            return true;
        }
        if (!second && pl.duress >= 0) duressCheck(p, true);  // Extreme Duress: a strategic action
        announce(int8_t(choice - 10), pn(p) + " plays " + strategyName(choice - 10) + ".");
        {
            // Coup d'Etat: another player may stop the strategic action.
            WinCtx w;
            w.w = Win::StrategicAction;
            w.a = p;
            w.value = choice - 10;
            if (cardHas(window(w), "coup")) {
                say(p, pn(p) + "'s turn ends: the strategic action doesn't happen.");
                turnEnded_ = true;
                return true;
            }
        }
        if ((G.modules & kModDiscordant) && dsBlackmail(p, choice - 10)) {  // Blackmail Programs (Florzen)
            turnEnded_ = true;
            return true;
        }
        strategicAction(p, choice - 10);
        return true;
    }
}

// =================================================================== status phase
void Rules::statusPhase() {
    G.phase = Phase::Status;
    E.log(-1, "Status phase", EventKind::Phase);
    mitosis();  // Arborec: start of the status phase
    wormholeGeneratorStatus();  // (0.44.9: the original Wormhole Generator, games without the Codex)
    if (G.modules & kModDiscordant) dsStatusStart();  // Blessing of the Queens returns, Brikk Strain (Discordant Stars)
    if (G.modules & kModThundersEdge) statusStart6();  // Hydrothermal Mining, Radical Advancement
    if (G.events) eventsStatusStart();                 // Monuments to the Ages (galactic event)
    if ((G.modules & kModThundersEdge) && G.fixes >= 5) scarStatusStart();  // entropic scars (0.43.4)
    // Negotiations: loans that fall due this round (0.30.0, owner's tester: new games pay them back
    // in the agenda phase instead - X-1 debts included)
    if (!(G.extras & 16)) loansDue();
    bountyWindow();  // Bounty Contracts (Uncharted Space)
    scoringStep();
    bounty_.clear();
    if (!eventOn(G, kEvCivilized)) revealObjective();  // (Civilized Society: they are all face up already)
    // LRR 81.3: draw action cards (Neural Motivator: 2)
    for (int p : initiativeOrder(G)) drawActionCards(p, owns(p, "nm") ? 2 : 1, "status phase");
    if (G.modules & kModThundersEdge) btStatusDraws();  // Slumberstate Computing (Titans breakthrough)
    for (SystemState& s : G.systems) s.tokens = 0;
    for (int p : initiativeOrder(G)) {
        int n = 2 + (owns(p, "hm") ? 1 : 0) + (fac(p, "sol") ? 1 : 0);  // Hyper Metabolism, Versatile
        n += extraStatusTokens(p);                                        // Cybernetic Enhancements
        // Deliberate Action (Kolume, Discordant Stars): their tokens can't be rearranged
        const bool deliberate = (G.modules & kModDiscordant) && fac(p, "kolume");
        gainTokens(p, n, !deliberate, "Status phase: gain " + std::to_string(n) + " command tokens" + (deliberate ? "." : " and rearrange."));
    }
    for (PlanetState& pl : G.planets) pl.exhausted = false;
    for (Player& p : G.players) p.exhaustedTechs.clear();
    for (Player& p : G.players) {  // Thunder's Edge: Custodia Vigilia and the breakthroughs ready too
        p.custodiaExhausted = false;
        p.dsGhotiExhausted = false;  // the Ghoti planet card (Discordant Stars)
        p.btExhausted = false;
    }
    readyLeaders();  // agents
    statusRelics();  // relics and legendary planet abilities ready; Crown of Emphidia
    for (SystemState& s : G.systems) {
        for (Force& f : s.space)
            for (int t = 0; t < kUnitTypes; ++t) f.dmg[t] = 0;
    }
    for (PlanetState& pl : G.planets)
        for (Force& f : pl.units)
            for (int t = 0; t < kUnitTypes; ++t) f.dmg[t] = 0;
    // LRR 81.8: return strategy cards (Political Stability keeps them).
    {
        WinCtx w;
        w.w = Win::ReturnStrategyCards;
        for (const Played& x : window(w)) {
            G.players[x.player].keepsCards = true;
            say(x.player, pn(x.player) + " keeps their strategy cards for the next round.");
        }
    }
    for (Player& p : G.players) {
        if (p.keepsCards) {
            p.cardsUsed = 0;
            continue;
        }
        p.cards.clear();
        p.cardsUsed = 0;
    }
    for (int c = 0; c < kStrategyCards; ++c) G.cardOwner[c] = -1;
    for (int q = 0; q < G.numPlayers; ++q)
        for (int8_t c : G.players[q].cards) G.cardOwner[c] = int8_t(q);
    // Minister of Policy: its owner draws an action card at the end of the status phase.
    int policy = lawOwner(G, "minister_policy");
    if (policy >= 0 && !G.players[policy].eliminated) drawActionCards(policy, 1, "Minister of Policy");
    statusFactions();  // Genesis
    if (G.modules & kModThundersEdge) statusEnd6();  // Share Knowledge goes back, ocean cards ready
    bioplasmosis();    // Arborec
    oracle();          // Naalu hero
    // Gift of Prescience goes back to the Naalu player; the "0" token comes off the strategy card.
    for (int q = 0; q < G.numPlayers; ++q)
        for (int16_t note : std::vector<int16_t>(G.players[q].notesInPlay))
            if (noteType(note) == kNoteFaction && isFaction(G, noteOwner(note), "naalu")) returnNote(q, note);
    G.naaluToken = -1;
    eventsStatusEnd();  // Civilized Society: the game may end here (galactic events)
}

}  // namespace ti
