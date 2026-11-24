// League mode test (0.80.0, TI4_UITEST_ONLY=leaguerun): a whole league played with the computer at every seat (yours too) -
// the table, the faction draft, the game's Setup, the game itself at full speed, then the results going into the league
// (places, shared places, bonuses, the rivals' memory) until the league is won or lost. TI4_LEAGUE=<id> picks the league
// (default bronze), TI4_LEAGUE_GAMES=N stops after N games. Prints every game's table and the standings.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/settings.h"
#include "engine/data.h"
#include "engine/rng.h"
#include "game/league_run.h"
#include "game/leagues.h"
#include "game/profile.h"
#include "game/session.h"

int leagueRunTest(const std::string& outDir) {
    (void)outDir;
    const char* which = std::getenv("TI4_LEAGUE") ? std::getenv("TI4_LEAGUE") : "bronze";
    const league::LeagueDef* d = league::find(which);
    if (!d) {
        std::printf("leaguerun: no league '%s'\n", which);
        return 0;
    }
    const int stopAfter = std::getenv("TI4_LEAGUE_GAMES") ? std::atoi(std::getenv("TI4_LEAGUE_GAMES")) : 99;
    Settings st;
    uint32_t seed = std::getenv("TI4_LEAGUE_SEED") ? uint32_t(std::atoi(std::getenv("TI4_LEAGUE_SEED"))) : 4242u;
    leaguerun::begin(d->id, seed);
    std::printf("leaguerun: %s - %d players, target %d, %d games\n", d->name, d->players, d->target, d->maxGames);
    int games = 0, problems = 0;
    for (; games < stopAfter && leaguerun::active(d->id); ++games) {
        const uint32_t ds = leaguerun::draftSeed(d->id, seed * 2654435761u + uint32_t(games));
        leaguerun::Table t = leaguerun::planTable(d, ds);
        int speaker = -1;
        if (d->factionDraft) {
            const std::vector<int> offer = leaguerun::offer(d, t, ds, false);
            std::vector<int> left = offer;
            ti::Rng r(ds ^ 0x5EA7u);
            const int n = int(t.seats.size()), start = r.below(n);
            for (int k = 0; k < n; ++k) {
                const int q = (start + k) % n;
                const int f = leaguerun::botPick(d, t.seats[size_t(q)], left, ds + uint32_t(k) * 977u);
                t.seats[size_t(q)].faction = f;
                left.erase(std::find(left.begin(), left.end(), f));
                speaker = q;
            }
        } else {
            if (leaguerun::keptFaction(d->id) < 0) leaguerun::setKeptFaction(d->id, leaguerun::offer(d, t, ds, false).front());
            leaguerun::dealFactions(d, t, leaguerun::keptFaction(d->id), ds);
        }
        std::printf("game %d:", games + 1);
        for (size_t q = 0; q < t.seats.size(); ++q) {
            const leaguerun::SeatPlan& s = t.seats[q];
            std::printf(" [%zu %s %s L%d%s]", q, s.who.empty() ? "bot" : s.who.c_str(), s.faction >= 0 ? ti::factionShortName(s.faction) : "?",
                        s.level, int(q) == speaker ? " speaker" : "");
            if (s.faction < 0) ++problems;
        }
        std::printf("  rival: %s\n", t.yourRival.c_str());
        ti::Setup s = leaguerun::makeSetup(d, t, speaker, st, seed + uint32_t(games) * 7919u);
        s.autoplay = true;  // (the computer plays your seat too)
        GameSession gs;
        gs.setSavePath(leaguerun::savePath(d->id));
        gs.setLeague(d->id);
        gs.setSpeed(3);
        gs.setCardPause(2);
        if (!gs.startNew(s)) {
            std::printf("  could not start: %s\n", gs.engine().crashMessage().c_str());
            ++problems;
            break;
        }
        if (speaker >= 0 && gs.game().speaker != speaker && !(s.draft && gs.game().round > 0)) {
            // (a galaxy draft keeps the speaker it was given too)
            std::printf("  speaker %d asked, %d in the game\n", speaker, gs.game().speaker);
        }
        uint32_t now = 1000;
        long steps = 0;
        while (!gs.over() && steps < 4000000) {
            if (gs.holding()) gs.release();
            gs.step(now += 40, true);
            ++steps;
        }
        if (!gs.over()) {
            std::printf("  the game didn't finish\n");
            ++problems;
            break;
        }
        if (!leaguerun::recordGame(gs)) {
            std::printf("  not recorded\n");
            ++problems;
            break;
        }
        if (leaguerun::recordGame(gs)) {
            std::printf("  recorded twice!\n");
            ++problems;
        }
        leaguerun::Outcome o;
        leaguerun::takeOutcome(o);
        std::printf("  round %d:", gs.game().round);
        for (const leaguerun::Row& r : o.rows)
            std::printf(" %d.%s(%s %dvp +%d+%d=%d)", r.place + 1, r.name.c_str(), ti::factionShortName(r.faction), r.vp, r.placePts, r.bonusPts, r.total);
        std::printf("\n  your bonuses:");
        for (const auto& b : o.yourBonuses)
            if (b.second) std::printf(" [%s]", b.first.c_str());
        std::printf("\n");
        if (!o.memory.empty()) std::printf("  %s\n", o.memory.c_str());
        for (const std::string& c : o.newCommanders) std::printf("  commander unlocked: %s\n", c.c_str());
        if (GameSession::fileExists(leaguerun::savePath(d->id))) {
            std::printf("  the save is still there!\n");
            ++problems;
        }
        if (o.ended) {
            std::printf("league %s after %d games: %s (winner %s, your points %d)\n", o.ended == 1 ? "WON" : "LOST", o.games,
                        o.ended == 1 ? "finished" : "back to game 1", o.winner.c_str(), o.points);
            for (const auto& w : o.finalTable) std::printf("  %s %d\n", w.first.c_str(), w.second);
            std::printf("  finished: %d, active again: %d, first win: %d\n", profile::finished(d->id) ? 1 : 0, leaguerun::active(d->id) ? 1 : 0,
                        o.firstWin ? 1 : 0);
            ++games;
            break;
        }
    }
    std::printf("standings:");
    for (const std::string& w : leaguerun::ranking(d)) std::printf(" %s=%d", w.c_str(), leaguerun::points(d->id, w));
    std::printf("\ncommanders for Casual Game: %zu\n", leaguerun::commanders().size());
    std::printf("leaguerun: %d games, %d problems\n", games, problems);
    profile::save();
    return games;
}
