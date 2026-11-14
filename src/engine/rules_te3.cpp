// Thunder's Edge relics (0.34.0): the ten its relic deck adds (with the three Codex IV ones). In the deck only with
// the module; The Quantumcore and The Silver Flame wait for the breakthroughs and the Fracture (part 3), The Triad
// for planet-like payments. Our wording in card_text.cpp; AI in ai/ai_explore.cpp.
//   Book of Latvinia      - on gain: up to 2 technologies without prerequisites; action: purge for a point (all 4
//                           specialties) or the speaker token
//   Circlet of the Void   - no gravity rift rolls, other anomalies don't stop movement (query.cpp); action: exhaust
//                           to explore a frontier token in a system without other players' ships
//   Heart of Ixth         - after a die is rolled: exhaust to add or take 1 (heartOfIxth, from confusedSage's hooks)
//   Lightrail Ordnance    - space docks: Space Cannon 5 (x2), also at adjacent systems (query.cpp unitDef)
//   Metali Void Armaments - an extra Anti-Fighter Barrage 6 (x3) in space combat (rules_combat.cpp)
//   Metali Void Shielding - one non-fighter ship per set of hits may sustain damage (query.cpp metaliSustain)
//   Neuraloop             - when a public objective is revealed: purge a relic to replace it (neuraloop)
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool rk(int r, const char* k) { return r >= 0 && r < kRelicCount && !std::strcmp(kRelics[r].key, k); }
}  // namespace

bool teRelicReady(int r) {  // in the relic deck (the module on): all ten since part 3
    (void)r;
    return true;
}

void Rules::teTakeRelic(int p, int r) {
    if (rk(r, "quantumcore")) {  // gain your breakthrough (synergy for every colour: query.cpp allSynergy)
        gainBreakthrough(p, "The Quantumcore");
        return;
    }
    if (!rk(r, "bookoflatvinia")) return;
    for (int n = 0; n < 2; ++n) {  // up to 2 technologies without prerequisites
        // (fixes 27, the Nekro review - owner 2026-11-14) Propagation: "When you would research a technology: Gain 3 command
        // tokens instead" - each one
        if (G.fixes >= 27 && fac(p, "nekro")) {
            const int prev = curRelic_;
            curRelic_ = r;
            const int k = pick(p, Tag::RelicChoice, "Book of Latvinia: research a technology without prerequisites?",
                               {{1, "Gain 3 command tokens", "Propagation: instead of researching."}, {-1, "No more", ""}},
                               n == 0 ? "Up to 2." : "1 more.");
            curRelic_ = prev;
            if (k < 0) return;
            propagation(p, 0);
            continue;
        }
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t) {
            const TechDef& td = kTechs[t];
            if (td.req[0] + td.req[1] + td.req[2] + td.req[3] != 0 || !techAvailableToResearch(G, p, t)) continue;
            opts.push_back({t, td.name, techSummary(t)});
        }
        if (opts.empty()) return;
        opts.push_back({-1, "No more", ""});
        const int prev = curRelic_;
        curRelic_ = r;
        const int t = pick(p, Tag::RelicChoice, "Book of Latvinia: research a technology without prerequisites?", opts,
                           n == 0 ? "Up to 2." : "1 more.");
        curRelic_ = prev;
        if (t < 0) return;
        researchTech(p, t, true);
    }
}

void Rules::teRelicOptions(int p, std::vector<Option>& opts) const {
    if (!(G.modules & kModThundersEdge)) return;
    const Player& pl = G.players[size_t(p)];
    for (int16_t r : pl.relics) {
        if (rk(r, "bookoflatvinia")) opts.push_back({4000 + r, "Relic: Book of Latvinia", relicText(r)});
        if (rk(r, "circletofthevoid") && !pl.relicExhausted(r) && !circletSystems(p).empty())
            opts.push_back({4000 + r, "Relic: Circlet of the Void", relicText(r)});
        if (rk(r, "thesilverflame")) opts.push_back({4000 + r, "Relic: The Silver Flame", relicText(r)});
    }
}

std::vector<int> Rules::circletSystems(int p) const {
    std::vector<int> out;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].frontier && !otherShipsIn(G, p, int(s))) out.push_back(int(s));
    return out;
}

bool Rules::teRelicAction(int p, int r) {
    if (rk(r, "bookoflatvinia")) {
        int skips = 0;
        for (int x : controlledPlanets(G, p)) skips |= planetSkips(G, x);
        loseRelic(p, r, true);
        if ((skips & 15) == 15) {
            gainVP(p, 1, "Book of Latvinia");
        } else {
            G.speaker = int8_t(p);
            say(p, pn(p) + " takes the speaker token (Book of Latvinia).");
        }
        return true;
    }
    if (rk(r, "thesilverflame")) {  // a 10: 1 victory point; otherwise the home system is purged
        loseRelic(p, r, true);
        RollResult rr;
        const int v = E.rng().d10();
        rr.dice.push_back(int8_t(v));
        rr.need.push_back(10);
        rr.unit.push_back(-1);
        rr.bonus9.push_back(0);
        recount(rr);
        reportRoll(p, -1, "The Silver Flame", rr);
        if (v == 10) {
            gainVP(p, 1, "The Silver Flame");
        } else {
            Player& pl = G.players[size_t(p)];
            const int h = pl.homeSystem;
            say(p, pn(p) + "'s home system burns in the Silver Flame: it is purged with every unit in it.", h);
            if (h >= 0) {
                for (int q = 0; q < kSides; ++q) G.systems[size_t(h)].space[size_t(q)] = Force();
                const std::vector<int16_t> ps = G.systems[size_t(h)].planets;
                for (int16_t x : ps) {
                    for (int q = 0; q < kSides; ++q) G.planets[size_t(x)].units[size_t(q)] = Force();
                    destroyPlanet(x, p, "the Silver Flame");
                }
                G.systems[size_t(h)].tokens = 0;
            }
            pl.noPublics = true;
            fractureEnter(p, false);
            checkEliminations();
        }
        return true;
    }
    if (rk(r, "circletofthevoid")) {
        std::vector<Option> opts;
        for (int s : circletSystems(p)) opts.push_back({s, sysName(s), "frontier token"});
        if (opts.empty()) return false;
        const int prev = curRelic_;
        curRelic_ = r;
        const int s = pick(p, Tag::RelicChoice, "Circlet of the Void: explore the frontier token in which system?", opts, "", true);
        curRelic_ = prev;
        G.players[size_t(p)].exhaustedRelics.push_back(int16_t(r));
        relicUsedEvent(p, r, false);
        exploreFrontier(p, s);
        return true;
    }
    return false;
}

// Heart of Ixth: after a die is rolled, its holder may exhaust it to add 1 to it or take 1 from it.
void Rules::heartOfIxth(int roller, int other, int sys, RollResult& r) {
    if (!(G.modules & kModThundersEdge) || r.dice.empty()) return;
    const int relic = findRelic("heartofixth");
    for (int q = 0; q < G.numPlayers; ++q) {
        if (!relicReady(q, "heartofixth") || G.players[size_t(q)].eliminated) continue;
        std::vector<Option> opts;
        for (size_t i = 0; i < r.dice.size(); ++i) {
            const int v = r.dice[i], need = r.need[i];
            if (v < need && v + 1 >= need) opts.push_back({int(i) * 2 + 1, "Die " + std::to_string(i + 1) + ": " + std::to_string(v) + " -> " +
                                                                               std::to_string(v + 1) + " (a hit)", ""});
            if (v >= need && v - 1 < need) opts.push_back({int(i) * 2, "Die " + std::to_string(i + 1) + ": " + std::to_string(v) + " -> " +
                                                                           std::to_string(v - 1) + " (a miss)", ""});
        }
        if (opts.empty()) continue;
        opts.push_back({-1, "Keep the Heart of Ixth", ""});
        G.sageDice = r.dice;  // (the AI reads the roll there, as for Confused Sage)
        G.sageNeed = r.need;
        G.sageRoller = int8_t(roller);
        G.sageTarget = int8_t(other);
        const int prev = curRelic_;
        curRelic_ = relic;
        const int c = pick(q, Tag::RelicChoice, "Heart of Ixth: change one die of " + pn(roller) + "'s roll?", opts);
        curRelic_ = prev;
        G.sageDice.clear();
        G.sageNeed.clear();
        G.sageRoller = G.sageTarget = -1;
        if (c < 0) continue;
        const size_t i = size_t(c / 2);
        r.dice[i] = int8_t(r.dice[i] + (c % 2 ? 1 : -1));
        recount(r);
        G.players[size_t(q)].exhaustedRelics.push_back(int16_t(relic));
        relicUsedEvent(q, relic, false);
        say(q, pn(q) + " changes a die to " + std::to_string(r.dice[i]) + " (Heart of Ixth).", sys);
        reportRoll(roller, sys, "After the Heart of Ixth", r);
    }
}

// Neuraloop: when a public objective is revealed, its holder may purge a relic to swap it for a random objective
// from a deck of their choice (it becomes a public objective, even a secret one).
void Rules::neuraloop(int obj) {
    if (!(G.modules & kModThundersEdge)) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (!holdsRelic(q, "neuraloop") || G.players[size_t(q)].eliminated) continue;
        std::vector<Option> opts;
        for (int16_t r : G.players[size_t(q)].relics) opts.push_back({r, std::string("Purge ") + kRelics[r].name, ""});
        opts.push_back({-1, "Keep it", ""});
        const int prev = curRelic_;
        curRelic_ = findRelic("neuraloop");
        curValue_ = obj;
        const int r = pick(q, Tag::RelicChoice, std::string("Neuraloop: replace ") + kObjectives[obj].name + "?", opts,
                           "Purge one of your relics: the objective is discarded and a random one from a deck you choose takes its place.");
        curValue_ = 0;
        if (r < 0) {
            curRelic_ = prev;
            continue;
        }
        std::vector<Option> decks;
        if (!G.stage1Deck.empty() || !G.stage1Rest.empty()) decks.push_back({1, "Stage I objectives", ""});
        if (!G.stage2Deck.empty() || !G.stage2Rest.empty()) decks.push_back({2, "Stage II objectives", ""});
        if (!G.secretDeck.empty()) decks.push_back({3, "Secret objectives", ""});
        if (decks.empty()) {
            curRelic_ = prev;
            return;
        }
        const int which = pick(q, Tag::RelicChoice, "Neuraloop: draw the new objective from which deck?", decks);
        curRelic_ = prev;
        loseRelic(q, r, true);
        std::vector<int16_t>& deck = which == 1 ? (!G.stage1Deck.empty() ? G.stage1Deck : G.stage1Rest)
                                    : which == 2 ? (!G.stage2Deck.empty() ? G.stage2Deck : G.stage2Rest)
                                                 : G.secretDeck;
        const int k = E.rng().below(int(deck.size()));
        const int16_t o = deck[size_t(k)];
        deck.erase(deck.begin() + k);
        E.revealed();
        auto it = std::find(G.publicObjectives.begin(), G.publicObjectives.end(), int16_t(obj));
        if (it != G.publicObjectives.end()) *it = o;
        say(q, pn(q) + " purges a relic: " + kObjectives[obj].name + " is replaced by " + kObjectives[o].name + " (Neuraloop).");
        Event e;
        e.kind = EventKind::ObjectiveRevealed;
        e.count = int16_t(o);
        E.emit(std::move(e));
        obj = o;
    }
}

}  // namespace ti
