#include "game/league_run.h"

#include <algorithm>
#include <cstring>

#include "core/log.h"
#include "core/settings.h"
#include "engine/data.h"
#include "engine/map.h"
#include "engine/personality.h"
#include "engine/rng.h"
#include "engine/state.h"
#include "game/habits.h"
#include "game/profile.h"
#include "game/session.h"

namespace leaguerun {

namespace {

std::string K(const std::string& id, const std::string& k) { return "L." + id + "." + k; }
std::string R(const std::string& rival, const std::string& k) { return "R." + rival + "." + k; }

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(',', a);
        v.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}
std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s;
}
uint32_t hashOf(const std::string& s, uint32_t seed) {
    uint32_t h = 2166136261u ^ seed;
    for (char ch : s) h = (h ^ uint8_t(ch)) * 16777619u;
    return h;
}

bool favoured(const LeagueDef* d, int f) { return f >= 0 && league::listHas(d->favoured, ti::kFactions[f].key); }

// a weighted draw without putting back: favoured factions count 3 in a "weighted" pool
int draw(const LeagueDef* d, std::vector<int>& from, ti::Rng& r) {
    if (from.empty()) return -1;
    std::vector<int> w(from.size(), 1);
    int total = 0;
    for (size_t i = 0; i < from.size(); ++i) {
        if (d->pool == league::kPoolWeighted && favoured(d, from[i])) w[i] = 3;
        total += w[i];
    }
    int k = r.below(total);
    size_t i = 0;
    while (i + 1 < from.size() && k >= w[i]) k -= w[i++];
    const int f = from[i];
    from.erase(from.begin() + long(i));
    return f;
}

int pickArch(const LeagueDef* d, ti::Rng& r) {
    std::vector<int> fav, ok;
    for (int a = 0; a < int(ti::Arch::Count); ++a) {
        const char* key = ti::kArchetypes[a].key;
        if (league::listHas(d->persBan, key)) continue;
        ok.push_back(a);
        if (league::listHas(d->persFav, key)) fav.push_back(a);
    }
    if (ok.empty()) return -2;  // every archetype switched off (The Grid): the plain program
    if (!fav.empty() && r.below(3) < 2) return fav[size_t(r.below(int(fav.size())))];
    return ok[size_t(r.below(int(ok.size())))];
}

// the bots' difficulty: the league's, rising through the league, or mixed per seat
int levelFor(const LeagueDef* d, int game, uint32_t seatHash) {
    switch (d->difficulty) {
        case league::kRising: return 1 + std::min(2, game * 3 / std::max(1, d->maxGames));
        case league::kMixed: return 1 + int(seatHash % 3u);
        default: return std::max(0, std::min(int(ti::kAiLevelAI), d->difficulty));
    }
}

bool pending_ = false;
Outcome outcome_;

}  // namespace

// ------------------------------------------------------------------ members
std::vector<Member> members(const LeagueDef* d) {
    std::vector<Member> v;
    Member you;
    you.id = "you";
    you.name = "You";
    v.push_back(you);
    if (!d) return v;
    const std::vector<int> pool = factionPool(d);
    const std::vector<const league::RivalDef*> rs = league::rivalsOf(d->id);
    if (d->rivals > 0 && !rs.empty()) {
        for (const league::RivalDef* r : rs) {
            Member m;
            m.id = r->id;
            m.name = r->name;
            m.rival = r;
            const int f = r->faction && *r->faction ? ti::findFaction(r->faction) : -1;
            m.faction = std::find(pool.begin(), pool.end(), f) != pool.end() ? f : -1;
            m.arch = r->arch;
            v.push_back(m);
        }
    } else {  // no named rivals (The Grid): the programs at the table are the standings
        for (int k = 1; k < d->players; ++k) {
            Member m;
            m.id = "prog" + std::to_string(k);
            m.name = "Program 0" + std::to_string(k);
            m.arch = -2;
            v.push_back(m);
        }
    }
    return v;
}
const Member* member(const std::vector<Member>& v, const std::string& id) {
    for (const Member& m : v)
        if (m.id == id) return &m;
    return nullptr;
}

// ---------------------------------------------------------------------- run
bool active(const std::string& id) { return profile::getInt(K(id, "on")) == 1; }
void begin(const std::string& id, uint32_t seed) {
    profile::erase("L." + id + ".");
    profile::setInt(K(id, "on"), 1);
    profile::setInt(K(id, "game"), 0);
    profile::setInt(K(id, "mapseed"), int(1 + (seed % 999983u)));  // a seeded league keeps one map for the whole run
    profile::save();
    std::remove(savePath(id).c_str());
}
void giveUp(const std::string& id) {
    std::remove(savePath(id).c_str());
    profile::erase("L." + id + ".");
    profile::save();
}
int gamesPlayed(const std::string& id) { return profile::getInt(K(id, "game")); }
int points(const std::string& id, const std::string& who) { return profile::getInt(K(id, "pts." + who)); }
int lastPlace(const std::string& id, const std::string& who) { return profile::getInt(K(id, "last." + who), -1); }
int moved(const std::string& id, const std::string& who) { return profile::getInt(K(id, "move." + who)); }
int played(const std::string& id, const std::string& who) { return profile::getInt(K(id, "played." + who)); }
std::string savePath(const std::string& id) {
    const std::string& p = GameSession::savePath();
    const size_t slash = p.find_last_of("/\\");
    return (slash == std::string::npos ? std::string() : p.substr(0, slash + 1)) + "league-" + id + ".ti4";
}
bool gameSaved(const std::string& id) { return GameSession::fileExists(savePath(id)); }
int keptFaction(const std::string& id) { return profile::getInt(K(id, "faction"), -1); }
void setKeptFaction(const std::string& id, int f) {
    profile::setInt(K(id, "faction"), f);
    profile::save();
}
int galaxyChoice(const std::string& id) { return profile::getInt(K(id, "galaxy"), 1); }
void setGalaxyChoice(const std::string& id, int v) {
    profile::setInt(K(id, "galaxy"), v);
    profile::save();
}

std::vector<std::string> ranking(const LeagueDef* d) {
    std::vector<Member> ms = members(d);
    std::vector<std::string> ids;
    for (const Member& m : ms) ids.push_back(m.id);
    if (!d) return ids;
    std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) { return points(d->id, a) > points(d->id, b); });
    return ids;
}

std::string yourRival(const LeagueDef* d) {
    if (!d) return "";
    const std::vector<Member> ms = members(d);
    const int mine = points(d->id, "you");
    std::string best;
    int bestGap = 1 << 30;
    for (const Member& m : ms) {
        if (m.id == "you" || !m.rival) continue;  // (programs aren't rivals)
        const int gap = points(d->id, m.id) - mine;
        const int score = std::abs(gap) * 2 + (gap < 0 ? 1 : 0);  // the nearest; on a tie, the one above you
        if (score < bestGap) bestGap = score, best = m.id;
    }
    return best;
}

Career career(const std::string& rival) {
    Career c;
    c.games = profile::getInt(R(rival, "games"));
    c.youAbove = profile::getInt(R(rival, "above"));
    c.theyAbove = profile::getInt(R(rival, "below"));
    c.broken = profile::getInt(R(rival, "broken"));
    return c;
}

bool beaten(const std::string& rival) { return profile::getInt(R(rival, "beaten")) == 1; }
std::vector<const league::RivalDef*> commanders() {
    std::vector<const league::RivalDef*> v;
    for (const league::RivalDef* r : league::allRivals())
        if (profile::unlockAll() || beaten(r->id)) v.push_back(r);
    return v;
}
int grudgeOf(const std::string& rival) {
    const Career c = career(rival);
    return std::min(25, 5 * c.broken + c.youAbove);
}

// -------------------------------------------------------------------- table
std::vector<int> factionPool(const LeagueDef* d) {
    std::vector<int> v;
    if (!d) return v;
    for (int f = 0; f < ti::kFactionCount; ++f) {
        const ti::FactionDef& fd = ti::kFactions[f];
        if (fd.source == ti::Source::ThundersEdge && (!(d->modules & ti::kModThundersEdge) || !std::strcmp(fd.key, "obsidian"))) continue;
        if (fd.source == ti::Source::Discordant && (!(d->modules & ti::kModDiscordant) || !ti::factionInPool(f, ti::kDsLevel))) continue;
        // (0.83.0: Blue Reverie factions come to the leagues as unlocks - 0.85.3, owner: "secret unlockables ... unavailable to the
        // bots but available when unlocked": in the leagues with the module, only the ones unlocked, for everyone)
        if (fd.source == ti::Source::BlueReverie &&
            (!(d->modules & ti::kModBlueReverie) || !ti::brInPool(f, ti::kBrLevel, profile::brUnlockedMask())))
            continue;
        if (league::listHas(d->banned, fd.key)) continue;
        v.push_back(f);
    }
    return v;
}

Table planTable(const LeagueDef* d, uint32_t seed) {
    Table t;
    if (!d) return t;
    ti::Rng r(seed ^ 0x1EA6u);
    const int n = std::max(3, std::min(8, d->players));
    const int game = gamesPlayed(d->id);
    t.human = std::max(0, ti::southernmostSeat(n, 0));
    t.yourRival = yourRival(d);
    std::vector<Member> ms = members(d);
    ms.erase(ms.begin());  // (you)
    const bool programs = !ms.empty() && !ms[0].rival;
    const int named = std::min<int>(int(ms.size()), programs ? n - 1 : std::min(d->rivals, n - 1));
    // your rival sits for sure; then the members who have played fewest games this run
    std::stable_sort(ms.begin(), ms.end(), [&](const Member& a, const Member& b) {
        if ((a.id == t.yourRival) != (b.id == t.yourRival)) return a.id == t.yourRival;
        const int pa = played(d->id, a.id), pb = played(d->id, b.id);
        if (pa != pb) return pa < pb;
        return hashOf(a.id, seed) < hashOf(b.id, seed);
    });
    ms.resize(size_t(std::max(0, named)));
    std::vector<int> others;
    for (int q = 0; q < n; ++q)
        if (q != t.human) others.push_back(q);
    r.shuffle(others);
    t.seats.assign(size_t(n), SeatPlan());
    t.seats[size_t(t.human)].who = "you";
    t.seats[size_t(t.human)].name = "You";
    for (size_t i = 0; i < others.size(); ++i) {
        SeatPlan& s = t.seats[size_t(others[i])];
        if (i < ms.size()) {
            s.who = ms[i].id;
            s.name = ms[i].name;
            s.signature = ms[i].faction;
            s.arch = ms[i].arch;
        } else {
            s.arch = pickArch(d, r);
        }
        s.level = levelFor(d, game, hashOf(s.who + std::to_string(others[i]), seed));
    }
    return t;
}

std::vector<int> offer(const LeagueDef* d, const Table& t, uint32_t seed, bool wholePool) {
    std::vector<int> pool = factionPool(d);
    if (wholePool || pool.size() <= t.seats.size() + 3) {
        std::sort(pool.begin(), pool.end(), [](int a, int b) { return std::strcmp(ti::factionShortName(a), ti::factionShortName(b)) < 0; });
        return pool;
    }
    ti::Rng r(seed ^ 0x0FF3u);
    const size_t want = t.seats.size() + 3;
    std::vector<int> v;
    if (d->pool == league::kPoolFavoured) {  // the favoured ones first; the rest only to fill the offer up
        std::vector<int> fav;
        for (int f : pool)
            if (favoured(d, f)) fav.push_back(f);
        while (!fav.empty() && v.size() < want) {
            const int f = draw(d, fav, r);
            v.push_back(f);
            pool.erase(std::find(pool.begin(), pool.end(), f));
        }
    }
    while (!pool.empty() && v.size() < want) v.push_back(draw(d, pool, r));
    std::sort(v.begin(), v.end(), [](int a, int b) { return std::strcmp(ti::factionShortName(a), ti::factionShortName(b)) < 0; });
    return v;
}

int botPick(const LeagueDef* d, const SeatPlan& s, const std::vector<int>& left, uint32_t seed) {
    if (left.empty()) return -1;
    if (s.signature >= 0 && std::find(left.begin(), left.end(), s.signature) != left.end()) return s.signature;
    std::vector<int> fav;
    for (int f : left)
        if (favoured(d, f)) fav.push_back(f);
    ti::Rng r(seed ^ hashOf(s.who + s.name, 0xB07u));
    if (!fav.empty() && r.below(3) < 2) return fav[size_t(r.below(int(fav.size())))];
    return left[size_t(r.below(int(left.size())))];
}

void dealFactions(const LeagueDef* d, Table& t, int yours, uint32_t seed) {
    std::vector<int> pool = factionPool(d);
    auto take = [&](int f) {
        auto it = std::find(pool.begin(), pool.end(), f);
        if (it != pool.end()) pool.erase(it);
    };
    for (SeatPlan& s : t.seats) s.faction = -1;
    t.seats[size_t(t.human)].faction = yours;
    take(yours);
    for (SeatPlan& s : t.seats)  // rivals keep their own factions
        if (s.faction < 0 && s.signature >= 0 && std::find(pool.begin(), pool.end(), s.signature) != pool.end()) {
            s.faction = s.signature;
            take(s.faction);
        }
    ti::Rng r(seed ^ 0xDEA1u);
    for (SeatPlan& s : t.seats)
        if (s.faction < 0 && !pool.empty()) s.faction = draw(d, pool, r);
}

uint32_t draftSeed(const std::string& id, uint32_t fresh) {
    int v = profile::getInt(K(id, "dseed"), 0);
    if (!v) {
        v = int(1 + (fresh % 2000000000u));
        profile::setInt(K(id, "dseed"), v);
        profile::save();
    }
    return uint32_t(v);
}

ti::Setup makeSetup(const LeagueDef* d, const Table& t, int speaker, const Settings& st, uint32_t seed) {
    ti::Setup s;
    const int n = int(t.seats.size());
    s.players = n;
    s.human = t.human;
    s.vpGoal = d->vp;
    s.humanColor = -1;
    s.trade = (d->x1 ? 1 | 8 : 0) | (d->freeRefills ? 2 : 0) | 4;
    const int galaxy = d->galaxy == league::kGalaxyChoice ? (galaxyChoice(d->id) ? league::kGalaxyDraft : league::kGalaxyRandom) : d->galaxy;
    s.draft = galaxy == league::kGalaxyDraft ? 1 : 0;
    s.mapSeed = galaxy == league::kGalaxySeeded ? uint32_t(std::max(1, profile::getInt(K(d->id, "mapseed"), 7))) : 0u;
    s.layout = 0;
    s.modules = d->modules;
    s.brMask = profile::brUnlockedMask();  // (0.85.3) only the Blue Reverie factions unlocked
    s.events = 0;
    s.extras = (d->kingslay ? 1 : 0) | 2 | 4 | 8 | 16 | 32 | 64 | (st.getInt("mecatol_contested", 1) ? 128 : 0);
    s.habits = habits::load(st);
    s.seed = seed;
    s.speaker = speaker;
    s.factions.assign(size_t(n), -1);
    s.colors.assign(size_t(n), -1);
    s.levels.assign(size_t(n), -1);
    s.archetypes.assign(size_t(n), -1);
    s.tribunii.assign(size_t(n), -1);
    s.grudges.assign(size_t(n), 0);
    s.commanders.assign(size_t(n), std::string());
    int count[8] = {};
    std::vector<std::string> who;
    for (int q = 0; q < n; ++q) {
        const SeatPlan& p = t.seats[size_t(q)];
        s.factions[size_t(q)] = p.faction;
        who.push_back(p.who);
        if (q == t.human) {
            const int c = st.getInt("color", -1);
            if (c >= 0 && c < ti::kPlayerColours && profile::colourUnlocked(c)) s.colors[size_t(q)] = c;
            continue;
        }
        s.levels[size_t(q)] = p.level;
        s.archetypes[size_t(q)] = p.arch;
        ++count[std::max(0, std::min(7, p.level))];
        if (!p.who.empty() && p.who.compare(0, 4, "prog") != 0) {  // a rival's grudge from the games before
            s.grudges[size_t(q)] = grudgeOf(p.who);
            s.commanders[size_t(q)] = p.who;  // (0.80.11: their name on the game's screens)
        }
    }
    s.aiLevel = int(std::max_element(count, count + 8) - count);
    profile::set(K(d->id, "seats"), join(who));
    profile::setInt(K(d->id, "human"), t.human);
    profile::set(K(d->id, "rival"), t.yourRival);
    profile::erase(K(d->id, "dseed"));
    profile::save();
    return s;
}

std::vector<std::string> seatNames(const std::string& id) {
    std::vector<std::string> v;
    const std::string seats = profile::get(K(id, "seats"));
    if (seats.empty()) return v;
    const std::vector<Member> ms = members(league::find(id));
    for (const std::string& w : split(seats)) {
        const Member* m = w == "you" ? nullptr : member(ms, w);
        v.push_back(m ? m->name : "");
    }
    return v;
}

// ------------------------------------------------------------------ results
bool recordGame(GameSession& gs) {
    const std::string id = gs.league();
    const LeagueDef* d = league::find(id);
    if (!d || !gs.engine().finished() || !active(id)) return false;
    const std::string seats = profile::get(K(id, "seats"));
    if (seats.empty()) return false;  // (already recorded: the seat plan goes with the first record)
    const ti::Game& g = gs.game();
    const int n = g.numPlayers;
    std::vector<std::string> who = split(seats);
    who.resize(size_t(n));
    const int human = gs.engine().setup().human;
    if (human >= 0 && human < n) who[size_t(human)] = "you";
    const std::vector<Member> ms = members(d);
    const std::string rivalId = profile::get(K(id, "rival"));
    int rivalSeat = -1;
    for (int q = 0; q < n; ++q)
        if (!rivalId.empty() && who[size_t(q)] == rivalId) rivalSeat = q;

    // places: the winner first, then by victory points; knocked-out players last; shared places split the points (rounded up)
    std::vector<int> order;
    for (int q = 0; q < n; ++q) order.push_back(q);
    auto key = [&](int q) { return g.players[size_t(q)].eliminated ? -1000 : q == g.winner ? 1000 : int(g.players[size_t(q)].vp); };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) > key(b); });
    std::vector<int> place(size_t(n), 0), placePts(size_t(n), 0);
    for (int i = 0; i < n;) {
        int j = i;
        while (j + 1 < n && key(order[size_t(j + 1)]) == key(order[size_t(i)])) ++j;
        int sum = 0;
        for (int k = i; k <= j; ++k) sum += league::placePoints(k);
        const int each = (sum + (j - i)) / (j - i + 1);
        for (int k = i; k <= j; ++k) place[size_t(order[size_t(k)])] = i, placePts[size_t(order[size_t(k)])] = each;
        i = j + 1;
    }

    // bonuses
    const bool te = (d->modules & ti::kModThundersEdge) != 0;
    auto planetOwner = [&](const char* key2) {
        for (const ti::PlanetState& p : g.planets)
            if (p.def >= 0 && !std::strcmp(ti::kPlanets[p.def].key, key2)) return int(p.owner);
        return -1;
    };
    const int mr = planetOwner("mr"), styx = planetOwner("styx");
    auto got = [&](const std::string& b, int q) -> bool {
        const ti::Player& pl = g.players[size_t(q)];
        if (b == "mecatol") return g.custodiansBy == q;
        if (b == "early") return g.winner == q && g.round < 7;
        if (b == "rival") {
            if (q == human) return rivalSeat >= 0 && place[size_t(q)] < place[size_t(rivalSeat)];
            return q == rivalSeat && human >= 0 && place[size_t(q)] < place[size_t(human)];
        }
        if (b == "promises") {
            int made = 0, broke = 0;
            for (const ti::Promise& p : g.promises)
                if (p.by == q) ++made, broke += p.state == ti::kPromiseBroken;
            return made > 0 && broke == 0 && pl.promisesBroken == 0;
        }
        if (b == "elim") {
            for (const ti::Player& o : g.players)
                if (o.eliminated && o.knockedOutBy == q) return true;
            return false;
        }
        if (b == "mecstyx") return te && mr == q && styx == q;
        if (b == "throne") {
            int k = 0;
            for (int16_t note : pl.notesInPlay) k += ti::noteType(note) == ti::kNoteSupportThrone;
            return k >= 3;
        }
        if (b == "fracture") {
            if (!te || !g.fractureIn) return false;
            int all = 0;
            for (const ti::PlanetState& p : g.planets) {
                if (p.system < 0 || !g.systems[size_t(p.system)].fracture || p.destroyed) continue;
                if (p.owner != q) return false;
                ++all;
            }
            return all > 0;
        }
        return false;
    };
    std::vector<int> bonus(size_t(n), 0);
    std::vector<std::pair<std::string, bool>> yours;
    for (const league::BonusDef& b : league::bonuses()) {
        const std::string bid = b.id;
        if ((bid == "mecstyx" || bid == "fracture") && !te) continue;
        if (bid == "rival" && rivalId.empty()) continue;
        for (int q = 0; q < n; ++q)
            if (!who[size_t(q)].empty() && got(bid, q)) bonus[size_t(q)] += b.points;
        yours.push_back({b.label, human >= 0 && got(bid, human)});
    }

    // the league: points, places, games played, the standings' moves
    const std::vector<std::string> before = ranking(d);
    for (int q = 0; q < n; ++q) {
        const std::string& w = who[size_t(q)];
        if (w.empty()) continue;
        profile::setInt(K(id, "pts." + w), points(id, w) + placePts[size_t(q)] + bonus[size_t(q)]);
        profile::setInt(K(id, "last." + w), place[size_t(q)]);
        profile::setInt(K(id, "played." + w), played(id, w) + 1);
    }
    const int games = gamesPlayed(id) + 1;
    profile::setInt(K(id, "game"), games);
    const std::vector<std::string> after = ranking(d);
    for (size_t i = 0; i < after.size(); ++i) {
        const size_t was = size_t(std::find(before.begin(), before.end(), after[i]) - before.begin());
        profile::setInt(K(id, "move." + after[i]), int(was) - int(i));
    }

    // the rivals' memory of you
    Outcome o;
    o.league = id;
    o.game = games - 1;
    int talk = -1, talkWhy = 0;  // 3 you broke a promise to it, 2 your rival, 1 finished just above you
    std::string memory;
    for (int q = 0; q < n; ++q) {
        const std::string& w = who[size_t(q)];
        const Member* m = member(ms, w);
        if (!m || !m->rival || q == human) continue;
        int broke = 0;
        for (const ti::Promise& p : g.promises)
            if (p.by == human && p.to == q && p.state == ti::kPromiseBroken) ++broke;
        profile::setInt(R(w, "games"), profile::getInt(R(w, "games")) + 1);
        if (human >= 0 && place[size_t(human)] < place[size_t(q)]) {
            profile::setInt(R(w, "above"), profile::getInt(R(w, "above")) + 1);
            if (!beaten(w)) {  // (0.80.11) beaten - finished above - for the first time: a commander for Casual Game
                profile::setInt(R(w, "beaten"), 1);
                o.newCommanders.push_back(w);
            }
        }
        if (human >= 0 && place[size_t(q)] < place[size_t(human)]) profile::setInt(R(w, "below"), profile::getInt(R(w, "below")) + 1);
        if (broke) profile::setInt(R(w, "broken"), profile::getInt(R(w, "broken")) + broke);
        const int why = broke ? 3 : q == rivalSeat ? 2 : (human >= 0 && place[size_t(q)] < place[size_t(human)]) ? 1 : 0;
        if (why > talkWhy || talk < 0) {
            talk = q, talkWhy = why;
            std::string facts;
            if (human >= 0 && place[size_t(human)] < place[size_t(q)]) facts = "you finished above it";
            else if (human >= 0 && place[size_t(q)] < place[size_t(human)]) facts = "it finished above you";
            else facts = "you finished level";
            if (broke) facts += std::string(", and you broke ") + (broke == 1 ? "a promise" : std::to_string(broke) + " promises") + " to it";
            if (g.players[size_t(q)].eliminated && g.players[size_t(q)].knockedOutBy == human) facts += ", and you knocked it out";
            memory = m->name + " will remember this game: " + facts + ". Grudge: " + m->rival->grudge;
        }
    }
    if (talk >= 0) {
        const Member* m = member(ms, who[size_t(talk)]);
        o.speaker = m->name;
        o.quote = m->rival->taunt;
        o.speakerFaction = g.players[size_t(talk)].faction;
        o.memory = memory;
    }
    for (int q : order) {
        Row r;
        r.who = who[size_t(q)];
        const Member* m = member(ms, r.who);
        r.you = q == human;
        r.named = m != nullptr;
        r.name = r.you ? "You" : m ? m->name : ti::factionShortName(g.players[size_t(q)].faction);
        r.faction = g.players[size_t(q)].faction;
        r.vp = g.players[size_t(q)].vp;
        r.place = place[size_t(q)];
        r.placePts = placePts[size_t(q)];
        r.bonusPts = r.named ? bonus[size_t(q)] : 0;
        r.total = r.named ? points(id, r.who) : 0;
        r.move = r.named ? moved(id, r.who) : 0;
        o.rows.push_back(r);
    }
    o.yourBonuses = yours;

    // the league's end: someone reached the target (the most points wins; you on a tie), or the game limit came
    const std::vector<std::string> rank = ranking(d);
    int top = -1;
    std::string lead;
    for (const std::string& w : rank) {
        const int p = points(id, w);
        if (p > top || (p == top && w == "you")) top = p, lead = w;
    }
    if (top >= d->target || games >= d->maxGames) {
        o.ended = lead == "you" ? 1 : 2;
        const Member* m = member(ms, lead);
        o.winner = m ? m->name : lead;
        o.points = points(id, "you");
        o.games = games;
        for (const std::string& w : rank) o.finalTable.push_back({w, points(id, w)});
    }
    profile::erase(K(id, "seats"));
    profile::erase(K(id, "rival"));
    profile::save();
    gs.deleteSave();
    o.firstWin = o.ended == 1 && !profile::finished(id);
    if (o.ended) settle(o, g.seed ^ 0x5E771Eu);
    LOG("league: %s game %d recorded (you %d points, ended %d)", id.c_str(), games, points(id, "you"), o.ended);
    outcome_ = o;
    pending_ = true;
    return true;
}

bool takeOutcome(Outcome& o) {
    if (!pending_) return false;
    o = outcome_;
    pending_ = false;
    return true;
}

void settle(const Outcome& o, uint32_t seed) {
    if (o.ended == 1) {
        profile::markFinished(o.league);
        profile::erase("L." + o.league + ".");
        profile::save();
    } else if (o.ended == 2) {
        begin(o.league, seed);  // lost: the league starts again from game 1 (the rivals remember)
    }
}

}  // namespace leaguerun
