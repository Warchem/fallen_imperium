// Scoring objectives: the status-phase scoring step, spending for "spend X"
// objectives, action-phase secret objectives and drawing secrets.
#include <algorithm>
#include <cstring>

#include <cstdio>
#include <cstdlib>
#include "engine/rules.h"

namespace ti {

std::vector<int> Rules::scorablePublic(int p) const {
    std::vector<int> v;
    if (!controlsHomePlanets(G, p) && !fac(p, "saar")) return v;  // LRR 61.16 (Nomadic: the Saar may score anyway)
    if (G.players[size_t(p)].noPublics) return v;  // The Silver Flame (Thunder's Edge)
    for (int16_t o : G.publicObjectives)
        if (!hasScored(G, p, o) && meetsObjective(G, p, o)) v.push_back(o);
    // Contract Network (Tnelis hero): another player's secret, scored like a public objective
    const int ps = G.players[size_t(p)].dsPubSecret;
    if (ps >= 0 && !hasScored(G, p, ps) && meetsObjective(G, p, ps)) v.push_back(ps);
    return v;
}

std::vector<int> Rules::scorableSecrets(int p, ObjPhase phase) const {
    std::vector<int> v;
    for (int16_t o : G.players[p].secrets)
        if (kObjectives[o].phase == phase && meetsObjective(G, p, o)) v.push_back(o);
    return v;
}

bool Rules::payObjectiveCost(int p, int obj) {
    Player& pl = G.players[p];
    if (!std::strcmp(kObjectives[obj].key, "fsn")) {  // discard 5 action cards
        if (pl.actionCards.size() < 5) return false;
        std::vector<Option> opts;
        for (size_t i = 0; i < pl.actionCards.size(); ++i)
            opts.push_back({int(i), kActionCards[pl.actionCards[i]].name, actionCardText(pl.actionCards[i])});
        std::vector<int> picks = pickMany(p, Tag::DiscardActionCard, std::string(kObjectives[obj].name) + ": discard 5 action cards",
                                          opts, 5, 5);
        std::sort(picks.rbegin(), picks.rend());
        for (int i : picks) {
            int c = pl.actionCards[i];
            pl.actionCards.erase(pl.actionCards.begin() + i);
            discardActionCard(p, c, false);
        }
    }
    if (!std::strcmp(kObjectives[obj].key, "dhw")) {  // purge 2 relic fragments
        if (fragmentCount(G, p) < 2) return false;
        purgeFragments(p, 2, -1, kObjectives[obj].name);
    }
    SpendNeed need = objectiveSpend(obj);
    if (need.tokens) {
        if (pl.tactic + pl.strategy < need.tokens) return false;
        spendTokensForObjective(p, need.tokens, kObjectives[obj].name);
    } else if (need.resources || need.influence || need.tradeGoods) {
        if (need.resources && need.influence) {
            if (!pay(p, need.resources, false, kObjectives[obj].name, true, need.influence, need.tradeGoods)) return false;
        } else if (need.resources) {
            if (!pay(p, need.resources, false, kObjectives[obj].name, true)) return false;
        } else if (need.influence) {
            if (!pay(p, need.influence, true, kObjectives[obj].name, true)) return false;
        } else {
            if (pl.tradeGoods < need.tradeGoods) return false;
            pl.tradeGoods = int8_t(pl.tradeGoods - need.tradeGoods);
        }
    }
    return true;
}

bool Rules::scoreObjective(int p, int obj) {
    Player& pl = G.players[p];
    if (!payObjectiveCost(p, obj)) return false;
    // (fixes 28, the tester's report) a leaked secret objective (Classified Document Leaks) scored by a player who doesn't hold
    // it is a public objective for them - not one of their 3 secret objectives (LRR 61.21)
    if (G.fixes >= 28 && kObjectives[obj].kind == 3 && std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(obj)) == pl.secrets.end() &&
        leakedSecret(G, obj))
        pl.scoredAsPublic.push_back(int16_t(obj));
    pl.scored.push_back(int16_t(obj));
    auto bountyPayout = [&]() {  // Bounty Contracts (Uncharted Space)
        for (const auto& b : bounty_)
            if (b.second == obj && G.phase == Phase::Status) {
                gainTG(b.first, 1);
                say(b.first, pn(b.first) + " gains 1 trade good (Bounty Contracts).");
            }
    };
    // (0.43.0, fixes >= 2: paid after the points are added - a Pillage asked in between saw an objective scored
    // without its points)
    if (G.fixes < 2) bountyPayout();
    bool purgeAfter = false;
    if (kObjectives[obj].kind == 3) {
        auto it = std::find(pl.secrets.begin(), pl.secrets.end(), obj);
        if (it != pl.secrets.end()) pl.secrets.erase(it);
        if (pl.dsPubSecret == obj) {  // Contract Network (Tnelis hero): scored - purged
            pl.dsPubSecret = -2;
            if (G.fixes < 2) purgeHero(p);
            else purgeAfter = true;  // (0.43.0 debug passes: after the points - the purge can ask questions, Soul Cage)
        }
    }
    {
        Event e;
        e.kind = EventKind::ObjectiveScored;
        e.player = int8_t(p);
        e.count = int16_t(obj);
        if (scoredAsSecret(G, p, obj)) e.text = pn(p) + " reveals and scores a secret objective: " + kObjectives[obj].name + ".";
        E.emit(e);
    }
    gainVP(p, kObjectives[obj].points,
           std::string(scoredAsSecret(G, p, obj) ? "secret objective " : "") + kObjectives[obj].name);
    if (G.fixes >= 2) bountyPayout();
    if (purgeAfter) purgeHero(p);
    if (scoredAsSecret(G, p, obj)) orthodoxyCheck(p);  // Zealous Orthodoxy (galactic event)
    if (scoredAsSecret(G, p, obj) && (G.modules & kModDiscordant) && G.players[size_t(p)].dsPubSecret != -2) dsSecretScored(p, obj);  // Turra Sveyar
    if (kObjectives[obj].kind != 3 && (G.modules & kModThundersEdge)) yinAscendant(p);  // Yin Ascendant (Yin breakthrough)
    return true;
}

void Rules::scoringStep() {
    for (int p : initiativeOrder(G)) {
        std::vector<int> pub = scorablePublic(p);
        if (std::getenv("TI4_SCORESTATS"))  // test only: how many publics each player qualifies for
            std::fprintf(stderr, "SCORE r%d p%d met %d vp %d secrets %d\n", G.round, p, int(pub.size()), G.players[p].vp,
                         int(G.players[p].secrets.size()));
        if (std::getenv("TI4_SCORESTATS"))
            for (int16_t o : G.publicObjectives)
                if (!hasScored(G, p, o)) {
                    ObjProgress pr = objectiveProgress(G, p, o);
                    std::fprintf(stderr, "OBJ r%d %s %d %d %d\n", G.round, kObjectives[o].key, meetsObjective(G, p, o) ? 1 : 0, pr.have, pr.need);
                }
        // Mobile Command (Ghoti, Discordant Stars): no public objectives while their flagship is off the board
        if ((G.modules & kModDiscordant) && fac(p, "ghoti") && unitsOnBoard(G, p, UnitType::Flagship) == 0) pub.clear();
        // Kadryn (Edyn commander, Discordant Stars): a secret objective instead of the public one
        if ((G.modules & kModDiscordant) && dsKadryn(p, int(pub.size()))) pub.clear();
        if (!pub.empty()) {
            std::vector<Option> opts;
            for (int o : pub) {
                SpendNeed s = objectiveSpend(o);
                std::string cost;
                if (s.any()) cost = " (costs what it says)";
                opts.push_back({o, std::string(kObjectives[o].name) + " - " + std::to_string(kObjectives[o].points) + " VP",
                                std::string(objectiveSummary(o)) + cost});
            }
            opts.push_back({-1, "Don't score a public objective", ""});
            int o = -1;
            for (int again = 0; again < 8; ++again) {  // 0.43.0 (fixes 2): backing out of the payment asks again
                o = pick(p, Tag::ScorePublic, "Score a public objective?", opts, "You may score 1 public objective this round.");
                if (o < 0 || scoreObjective(p, o) || G.fixes < 2) break;
            }
            // Civilized Society (galactic event): any number of them
            for (int guard = 0; o >= 0 && guard < 12 && eventOn(G, kEvCivilized); ++guard) {
                std::vector<Option> more;
                for (int x : scorablePublic(p))
                    more.push_back({x, std::string(kObjectives[x].name) + " - " + std::to_string(kObjectives[x].points) + " VP", objectiveSummary(x)});
                if (more.empty()) break;
                more.push_back({-1, "No more", ""});
                o = pick(p, Tag::ScorePublic, "Score another public objective?", more, "Civilized Society: no limit this round.");
                if (o >= 0) scoreObjective(p, o);
            }
        }
        // Psychographics (Augurs, Discordant Stars): a second public objective in place of the secret
        const bool psy = (G.modules & kModDiscordant) && dsPsychographics(p);
        std::vector<int> sec = psy ? std::vector<int>() : scorableSecrets(p, ObjPhase::Status);
        if (!sec.empty()) {
            std::vector<Option> opts;
            for (int o : sec) opts.push_back({o, std::string(kObjectives[o].name) + " - 1 VP", objectiveSummary(o)});
            opts.push_back({-1, "Don't score a secret objective", ""});
            for (int again = 0; again < 8; ++again) {  // 0.43.0 (fixes 2): backing out of the payment asks again
                int o = pick(p, Tag::ScoreSecret, "Score a secret objective?", opts, "You may score 1 secret objective this round.");
                if (o < 0 || scoreObjective(p, o) || G.fixes < 2) break;
            }
        }
        plotsWithinPlots(p, nullptr);  // the Firmament: other players' secrets (Thunder's Edge)
    }
}

void Rules::actionSecret(int p, const char* key) {
    if (p < 0 || p >= G.numPlayers) return;
    plotsWithinPlots(p, key);  // the Firmament: another player's secret (Thunder's Edge)
    Player& pl = G.players[p];
    for (int16_t o : pl.secrets) {
        if (std::strcmp(kObjectives[o].key, key) != 0) continue;
        // RULES-CHECK: action-phase secrets are scored automatically the moment they are achieved.
        scoreObjective(p, o);
        return;
    }
}

void Rules::spendTokensForObjective(int p, int count, const std::string& why) {
    // Any mix of tactic and strategy tokens (the objective says "and/or").
    Player& pl = G.players[p];
    std::vector<Option> opts;
    for (int t = count; t >= 0; --t) {
        int s = count - t;
        if (t > pl.tactic || s > pl.strategy) continue;
        opts.push_back({t, std::to_string(t) + " tactic + " + std::to_string(s) + " strategy",
                        "Leaves " + std::to_string(pl.tactic - t) + " tactic and " + std::to_string(pl.strategy - s) +
                            " strategy tokens."});
    }
    if (opts.empty()) return;
    int t = opts.size() == 1 ? opts[0].id : pick(p, Tag::TokenSpend, "Which tokens do you spend for " + why + "?", opts);
    int fromStrategy = count - t;
    // Scepter of Emelpar: one of the strategy tokens comes from reinforcements instead
    // (owner, RULES-CHECK 0.5.0 #3 - it still counts as spent for the objective).
    bool sc = fromStrategy > 0 && scepter(p);
    pl.tactic = int8_t(pl.tactic - t);
    spendStrategy(p, fromStrategy - (sc ? 1 : 0));
    say(p, pn(p) + " spends " + std::to_string(count) + " command tokens" +
               (sc ? " (one from reinforcements, with the Scepter of Emelpar)." : "."));
}

void Rules::drawSecret(int p) {
    if (G.secretDeck.empty()) return;
    int o = G.secretDeck.back();
    G.secretDeck.pop_back();
    Player& pl = G.players[p];
    pl.secrets.push_back(int16_t(o));
    E.revealed();
    say(p, pn(p) + " draws a secret objective.");
    if (G.modules & kModDiscordant) dsSecretDrawn(p);  // Plausible Deniability (Tnelis)
    // LRR 61.21: at most 3 secret objectives in total, scored ones included.
    int scoredSecrets = 0;
    for (int16_t x : pl.scored)
        if (scoredAsSecret(G, p, x)) ++scoredSecrets;  // (fixes 28: not a leaked one scored as a public objective)
    const int limit = 3 + (holdsRelic(p, "obsidian") ? 1 : 0)                  // The Obsidian: one more
                      + ((G.modules & kModDiscordant) && fac(p, "tnelis") ? 1 : 0);  // Information Brokers (Tnelis)
    if (int(pl.secrets.size()) + scoredSecrets > limit) {
        std::vector<Option> opts;
        for (int16_t x : pl.secrets) opts.push_back({x, kObjectives[x].name, objectiveSummary(x)});
        int drop = pick(p, Tag::DiscardSecret, "You may hold only " + std::to_string(limit) + " secret objectives: discard one", opts);
        pl.secrets.erase(std::find(pl.secrets.begin(), pl.secrets.end(), drop));
        G.secretDeck.insert(G.secretDeck.begin(), int16_t(drop));
    }
}

}  // namespace ti
