// League mode (0.80.0): the Tournament - the ladder of leagues from the player's profile, a league's hub (standings, the
// next game, your rival), the faction draft, the results after each game and the league won or lost. The logic lives in
// game/league_run, the drawing in app/league_ui; these scenes join them to the buttons and start the league's games.
#include <cstdlib>
#include <memory>

#include "app/app.h"
#include "app/league_ui.h"
#include "audio/audio.h"
#include "core/log.h"
#include "engine/data.h"
#include "game/league_run.h"
#include "game/leagues.h"
#include "game/profile.h"
#include "game/session.h"
#include "ui/hits.h"
#include "ui/emblems.h"
#include "ui/pcolors.h"
#include "ui/unlock_banner.h"

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

using namespace leagueui;
using leaguerun::Outcome;

namespace {

uint32_t freshSeed(uint32_t now) {
    static uint32_t n = 0;
    return (now + 7919u * ++n) * 2654435761u ^ 0x1EA6u;
}

std::vector<LadderEntry> ladderFromProfile() {
    std::vector<LadderEntry> v;
    for (const league::LeagueDef* d : league::all()) {
        LadderEntry e;
        e.def = d;
        if (leaguerun::active(d->id)) e.state = kInProgress, e.game = leaguerun::gamesPlayed(d->id), e.points = leaguerun::points(d->id, "you");
        else if (profile::finished(d->id)) e.state = kFinished;
        else e.state = profile::leagueOpen(d->id) ? kOpen : kLocked;
        v.push_back(e);
    }
    return v;
}

// a little note at the bottom of a screen for a while
struct Toast {
    std::string text;
    uint32_t until = 0;
    void show(const std::string& t, uint32_t now) { text = t, until = now + 2600; }
    void draw(Canvas& c, uint32_t now) const {
        if (!text.empty() && int32_t(until - now) > 0) drawToast(c, text);
    }
};

// (test only) TI4_LEAGUE_WATCH: the computer plays your seat; "1" at full speed, so the scripts reach the results
bool testWatch(App& app) {
    const char* w = std::getenv("TI4_LEAGUE_WATCH");
    if (!w) return false;
    if (std::string(w) == "1") app.settings().setInt("ai_speed", 3);
    return true;
}

// start (or continue) one of the league's games on top of the hub; false + why when it can't
bool launch(App& app, const league::LeagueDef* d, leaguerun::Table t, int speaker, uint32_t seed, std::string* err) {
    ti::Setup s = leaguerun::makeSetup(d, t, speaker, app.settings(), seed);
    if (testWatch(app)) s.autoplay = true;
    std::unique_ptr<GameSession> gs(new GameSession());
    gs->setSavePath(leaguerun::savePath(d->id));
    gs->setLeague(d->id);
    if (!gs->startNew(s)) {
        if (err) *err = "The game could not start: " + gs->engine().crashMessage();
        LOG("league: %s could not start: %s", d->id, gs->engine().crashMessage().c_str());
        return false;
    }
    LOG("league: %s game %d started (%d players, speaker %d)", d->id, leaguerun::gamesPlayed(d->id) + 1, s.players, speaker);
    app.push(makeGameScene(std::move(gs)));
    return true;
}
bool resume(App& app, const league::LeagueDef* d, std::string* err) {
    testWatch(app);
    std::unique_ptr<GameSession> gs(new GameSession());
    gs->setSavePath(leaguerun::savePath(d->id));
    gs->setLeague(d->id);
    std::string why;
    if (!gs->loadSaved(&why)) {
        if (err) *err = "The saved game could not be loaded: " + why;
        return false;
    }
    app.push(makeGameScene(std::move(gs)));
    return true;
}

// ------------------------------------------------------------------ rivals / rules
class RivalsScene : public Scene {
public:
    explicit RivalsScene(const league::LeagueDef* d) : d_(d) {
        if (d)
            for (const league::RivalDef* r : league::rivalsOf(d->id)) {
                RivalCard k;
                k.def = r;
                const leaguerun::Career c = leaguerun::career(r->id);
                k.games = c.games, k.youAbove = c.youAbove, k.theyAbove = c.theyAbove, k.broken = c.broken;
                k.commander = leaguerun::beaten(r->id);
                cards_.push_back(k);
            }
    }
    void update(App& app, const Input& in, uint32_t) override {
        const int n = int(cards_.size());
        if (n) {
            int s = sel_;
            if (in.pressed(Btn::Right)) s = (s + 1) % n;
            if (in.pressed(Btn::Left)) s = (s + n - 1) % n;
            if (in.pressed(Btn::Down)) s = std::min(n - 1, s + 2);
            if (in.pressed(Btn::Up)) s = std::max(0, s - 2);
            if (in.pressed(Btn::R1)) s = std::min(n - 1, (s / 6 + 1) * 6);
            if (in.pressed(Btn::L1)) s = std::max(0, (s / 6 - 1) * 6);
            if (s != sel_) sel_ = s, audio::play(audio::Sfx::Toggle);
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu) || in.pressed(Btn::A)) app.pop();
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(&sel_);
        drawRivals(c, d_, cards_, sel_, now);
        if (cards_.empty()) drawToast(c, "This league has no named rivals - only faceless programs.");
    }

private:
    const league::LeagueDef* d_;
    std::vector<RivalCard> cards_;
    int sel_ = 0;
};

class RulesScene : public Scene {
public:
    explicit RulesScene(const league::LeagueDef* d) : d_(d) {}
    void update(App& app, const Input& in, uint32_t) override {
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu) || in.pressed(Btn::A)) app.pop();
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(nullptr);
        drawRules(c, d_, now);
    }

private:
    const league::LeagueDef* d_;
};

// ------------------------------------------------------------------ unlocked (0.80.11)
// The unlock banners, one after another (A when each has come in): a rival commander beaten for the first time, a league's emblem
// and colour the first time it is won. Drawn on the whole width of the screen.
class UnlockScene : public Scene {
public:
    explicit UnlockScene(std::vector<unlockbanner::Unlock> q) : q_(std::move(q)) {}
    void update(App& app, const Input& in, uint32_t now) override {
        if (!started_) started_ = true, t0_ = now, audio::play(audio::Sfx::Victory, 0.7f);
        if ((in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Menu)) && unlockbanner::ready(now - t0_)) {
            if (++at_ >= q_.size()) {
                app.pop();
                return;
            }
            t0_ = now;
            audio::play(audio::Sfx::Victory, 0.7f);
        }
    }
    bool animating() const override { return true; }
    bool wide() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        setPickTarget(nullptr);
        c.fillRect(0, 0, c.width(), 480, rgb(4, 3, 12));
        if (at_ < q_.size()) unlockbanner::draw(c, q_[at_], started_ ? now - t0_ : 0, now);
        hits::addAction(Rect{0, 0, c.width(), 480}, [] {});
    }

private:
    std::vector<unlockbanner::Unlock> q_;
    size_t at_ = 0;
    bool started_ = false;
    uint32_t t0_ = 0;
};

// what this game unlocked: the rivals beaten for the first time, and a first win's emblem and colour
std::vector<unlockbanner::Unlock> unlocksOf(const Outcome& o) {
    std::vector<unlockbanner::Unlock> v;
    const league::LeagueDef* d = league::find(o.league);
    for (const std::string& id : o.newCommanders) {
        const league::RivalDef* r = league::rival(id);
        if (!r) continue;
        unlockbanner::Unlock u;
        u.kind = unlockbanner::kRival;
        u.category = "RIVAL COMMANDER";
        u.name = r->name;
        u.faction = r->faction && *r->faction ? ti::findFaction(r->faction) : -1;
        u.line = "You finished above them in a league game. Now you can seat them in a Casual Game - and they remember you.";
        u.source = d ? std::string(d->name) + ", game " + std::to_string(o.game + 1) : std::string();
        v.push_back(u);
    }
    if (o.ended == 1 && o.firstWin && d) {
        if (d->emblem && *d->emblem) {
            unlockbanner::Unlock u;
            u.kind = unlockbanner::kEmblem;
            u.category = "EMBLEM";
            u.emblem = d->emblem;
            u.emblemColour = d->color;
            u.name = emblems::name(d->emblem);
            u.line = "Shown under the title menu, for everyone to see.";
            u.source = std::string("Earned by winning the ") + d->name;
            v.push_back(u);
        }
        if (d->colour >= 0) {
            unlockbanner::Unlock u;
            u.kind = unlockbanner::kColour;
            u.category = "FACTION COLOUR";
            u.colour = d->colour;
            u.name = pcolours::info(d->colour).name;
            u.line = "A new colour for your fleets, borders and structures - pick it in Casual Game.";
            u.source = std::string("Earned by winning the ") + d->name;
            v.push_back(u);
        }
    }
    return v;
}

// ------------------------------------------------------------------ the league won or lost
class EndScene : public Scene {
public:
    explicit EndScene(const Outcome& o) : o_(o) {
        v_.def = league::find(o.league);
        v_.won = o.ended == 1;
        v_.points = o.points, v_.games = o.games, v_.winner = o.winner;
        for (const league::LeagueDef* x : league::all())
            if (x->after && o.league == x->after && x != v_.def) v_.opens.push_back(x);
        const std::vector<leaguerun::Member> ms = leaguerun::members(v_.def);
        for (const auto& w : o.finalTable) {
            Standing s;
            const leaguerun::Member* m = leaguerun::member(ms, w.first);
            s.you = w.first == "you";
            s.name = m ? m->name : w.first;
            s.faction = m ? m->faction : -1;
            s.points = w.second;
            v_.table.push_back(s);
        }
    }
    void update(App& app, const Input& in, uint32_t) override {
        if (!played_) {
            played_ = true;
            if (v_.won) audio::play(audio::Sfx::Victory, 0.8f);
        }
        if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();  // this screen
            app.pop();  // ...and the league's hub: back to the Tournament
        }
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(nullptr);
        drawWon(c, v_, now);
    }

private:
    Outcome o_;
    WonView v_;
    bool played_ = false;
};

// ------------------------------------------------------------------ a game's results
class ResultsScene : public Scene {
public:
    explicit ResultsScene(const Outcome& o) : o_(o) {
        v_.def = league::find(o.league);
        v_.game = o.game + 1;
        for (const leaguerun::Row& r : o.rows) {
            ResultRow x;
            x.name = r.name, x.faction = r.faction, x.you = r.you, x.named = r.named;
            x.vp = r.vp, x.place = r.place, x.placePts = r.placePts, x.bonusPts = r.bonusPts, x.total = r.total, x.move = r.move;
            v_.rows.push_back(x);
        }
        v_.yourBonuses = o.yourBonuses;
        v_.speaker = o.speaker, v_.quote = o.quote, v_.memory = o.memory, v_.speakerFaction = o.speakerFaction;
    }
    void update(App& app, const Input& in, uint32_t) override {
        if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            const Outcome o = o_;
            app.pop();
            if (o.ended) app.push(std::unique_ptr<Scene>(new EndScene(o)));
            std::vector<unlockbanner::Unlock> u = unlocksOf(o);  // (0.80.11) the unlock banners come first
            if (!u.empty()) app.push(std::unique_ptr<Scene>(new UnlockScene(std::move(u))));
        }
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(nullptr);
        drawResults(c, v_, now);
    }

private:
    Outcome o_;
    ResultView v_;
};

// ------------------------------------------------------------------ the faction draft (or your league faction)
class DraftScene : public Scene {
public:
    DraftScene(const league::LeagueDef* d, bool pickOnly, uint32_t seed) : d_(d), seed_(seed) {
        table_ = leaguerun::planTable(d, seed);
        v_.def = d;
        v_.game = leaguerun::gamesPlayed(d->id);
        v_.pickOnly = pickOnly;
        v_.offer = leaguerun::offer(d, table_, seed, pickOnly && d->pool == league::kPoolPlayer);
        if (!pickOnly) {
            ti::Rng r(seed ^ 0x5EA7u);
            const int n = int(table_.seats.size());
            const int start = r.below(n);  // a random seat picks first, then round the table
            int bots = 0;
            for (int k = 0; k < n; ++k) {
                const int q = (start + k) % n;
                seats_.push_back(q);
                DraftSeat s;
                s.you = q == table_.human;
                s.name = s.you ? "You" : !table_.seats[size_t(q)].name.empty() ? table_.seats[size_t(q)].name : "Bot " + std::to_string(++bots);
                v_.order.push_back(s);
            }
        }
        v_.sel = 0;
        nextFree(1);
    }
    void update(App& app, const Input& in, uint32_t now) override {
        if (!err_.text.empty() && int32_t(err_.until - now) <= 0) err_.text.clear();
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        const int n = int(v_.order.size());
        if (v_.pickOnly) {
            move(in);
            if (in.pressed(Btn::A) && v_.sel >= 0 && v_.sel < int(v_.offer.size())) {
                const int f = v_.offer[size_t(v_.sel)];
                if (lockedForYou(f, now)) return;
                leaguerun::setKeptFaction(d_->id, f);
                leaguerun::dealFactions(d_, table_, f, seed_);
                start(app, -1, now);
            }
            return;
        }
        if (v_.done) {
            if (in.pressed(Btn::A)) {
                for (int k = 0; k < n; ++k) table_.seats[size_t(seats_[size_t(k)])].faction = v_.order[size_t(k)].faction;
                start(app, seats_.back(), now);  // the last to pick is the speaker
            }
            return;
        }
        DraftSeat& cur = v_.order[size_t(v_.current)];
        if (cur.you) {
            move(in);
            if (in.pressed(Btn::A) && v_.sel >= 0 && v_.sel < int(v_.offer.size()) && free(v_.offer[size_t(v_.sel)]) &&
                !lockedForYou(v_.offer[size_t(v_.sel)], now)) {
                cur.faction = v_.offer[size_t(v_.sel)];
                audio::play(audio::Sfx::Confirm);
                advance(now);
            }
            return;
        }
        if (!due_) due_ = now + 650;
        if (in.pressed(Btn::A) || in.held(Btn::A)) due_ = std::min(due_, now + 120);
        if (int32_t(now - due_) >= 0) {
            std::vector<int> left;
            for (int f : v_.offer)
                if (free(f)) left.push_back(f);
            cur.faction = leaguerun::botPick(d_, table_.seats[size_t(seats_[size_t(v_.current)])], left, seed_ + uint32_t(v_.current) * 977u);
            audio::play(audio::Sfx::Toggle);
            advance(now);
        }
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(&v_.sel);
        drawDraft(c, v_, now);
        err_.draw(c, now);
    }

private:
    // (0.85.2) a Blue Reverie faction not earned yet: say how to earn it
    bool lockedForYou(int f, uint32_t now) {
        if (std::find(v_.locked.begin(), v_.locked.end(), f) == v_.locked.end()) return false;
        const league::LeagueDef* by = league::find(profile::factionLeague(ti::kFactions[f].key));
        err_.show("Finish the " + std::string(by ? by->name : "its league") + " to play as the " + ti::factionShortName(f) + ".", now);
        audio::play(audio::Sfx::Back);
        return true;
    }
    bool free(int f) const {
        for (const DraftSeat& s : v_.order)
            if (s.faction == f) return false;
        return true;
    }
    void nextFree(int dir) {
        const int m = int(v_.offer.size());
        for (int k = 0; k < m; ++k) {
            const int i = ((v_.sel + dir * k) % m + m) % m;
            if (free(v_.offer[size_t(i)])) {
                v_.sel = i;
                return;
            }
        }
    }
    void move(const Input& in) {
        const int m = int(v_.offer.size());
        if (!m) return;
        const int cols = m <= 9 ? 3 : m <= 16 ? 4 : 5;
        int s = v_.sel, dir = 0;
        if (in.pressed(Btn::Right)) s = (s + 1) % m, dir = 1;
        if (in.pressed(Btn::Left)) s = (s + m - 1) % m, dir = -1;
        if (in.pressed(Btn::Down)) s = s + cols < m ? s + cols : s % cols, dir = 1;
        if (in.pressed(Btn::Up)) s = s - cols >= 0 ? s - cols : std::min(m - 1, (m - 1) / cols * cols + s % cols), dir = -1;
        if (dir) {
            v_.sel = s;
            nextFree(dir);
            audio::play(audio::Sfx::Toggle);
        }
    }
    void advance(uint32_t now) {
        due_ = 0;
        if (++v_.current >= int(v_.order.size())) {
            v_.done = true;
            return;
        }
        nextFree(1);
        (void)now;
    }
    void start(App& app, int speaker, uint32_t now) {
        std::string why;
        app.pop();  // (this screen: the hub stays under the game)
        if (!launch(app, d_, table_, speaker, freshSeed(now), &why)) LOG("league: %s", why.c_str());
    }
    const league::LeagueDef* d_;
    uint32_t seed_;
    leaguerun::Table table_;
    std::vector<int> seats_;  // the picking order as seats
    DraftView v_;
    uint32_t due_ = 0;
    Toast err_;
};

// ------------------------------------------------------------------ a league's hub
class HubScene : public Scene {
public:
    explicit HubScene(const league::LeagueDef* d) : d_(d) {}
    void update(App& app, const Input& in, uint32_t now) override {
        if (int32_t(now - checkedAt_) >= 0) saved_ = leaguerun::gameSaved(d_->id), checkedAt_ = now + 1000;
        Outcome o;
        if (leaguerun::takeOutcome(o)) {  // back from a finished game
            app.push(std::unique_ptr<Scene>(new ResultsScene(o)));
            return;
        }
        if (confirm_) {
            if (in.pressed(Btn::A)) {
                leaguerun::giveUp(d_->id);
                audio::play(audio::Sfx::Back);
                app.pop();
            } else if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
                confirm_ = false;
            }
            return;
        }
        int s = sel_;
        if (in.pressed(Btn::Right) || in.pressed(Btn::Down)) s = (s + 1) % 4;
        if (in.pressed(Btn::Left) || in.pressed(Btn::Up)) s = (s + 3) % 4;
        if (s != sel_) sel_ = s, audio::play(audio::Sfx::Toggle);
        if (in.pressed(Btn::X) && d_->galaxy == league::kGalaxyChoice) {
            leaguerun::setGalaxyChoice(d_->id, leaguerun::galaxyChoice(d_->id) ? 0 : 1);
            audio::play(audio::Sfx::Toggle);
        }
        if (in.pressed(Btn::A)) {
            switch (sel_) {
                case 0: play(app, now); break;
                case 1: app.push(std::unique_ptr<Scene>(new RivalsScene(d_))); break;
                case 2: app.push(std::unique_ptr<Scene>(new RulesScene(d_))); break;
                case 3:
                    if (leaguerun::active(d_->id)) confirm_ = true;
                    else toast_.show("You haven't started this league yet.", now);
                    break;
            }
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(confirm_ ? nullptr : &sel_);
        drawHub(c, view(), now);
        toast_.draw(c, now);
        if (confirm_)
            drawConfirm(c, "Give up the league?",
                        "Your points in the " + std::string(d_->name) + " are lost and its saved game is deleted. Your rivals will remember.",
                        "Give up");
    }

private:
    HubView view() const {
        HubView v;
        v.def = d_;
        const std::string id = d_->id;
        const bool on = leaguerun::active(id);
        v.game = on ? leaguerun::gamesPlayed(id) : 0;
        const int kept = on ? leaguerun::keptFaction(id) : -1;
        for (const leaguerun::Member& m : leaguerun::members(d_)) {
            Standing s;
            s.you = m.id == "you";
            s.name = m.name;
            s.faction = s.you ? kept : m.faction;
            s.arch = s.you ? -1 : m.arch;
            if (on) s.points = leaguerun::points(id, m.id), s.lastPlace = leaguerun::lastPlace(id, m.id), s.move = leaguerun::moved(id, m.id);
            v.table.push_back(s);
        }
        const std::string rid = leaguerun::yourRival(d_);
        if (const league::RivalDef* r = rid.empty() ? nullptr : league::rival(rid)) {
            v.yourRival = r->name;
            v.rivalGap = (on ? leaguerun::points(id, rid) - leaguerun::points(id, "you") : 0);
            v.talker = r->name;
            v.talkerFaction = ti::findFaction(r->faction);
            v.quote = r->taunt;
        }
        v.sel = sel_;
        v.saved = on && saved_;
        v.galaxy = d_->galaxy == league::kGalaxyChoice ? leaguerun::galaxyChoice(id) : -1;
        v.pickFirst = !d_->factionDraft && kept < 0 && !v.saved;
        return v;
    }
    void play(App& app, uint32_t now) {
        const std::string id = d_->id;
        checkedAt_ = now;  // (look at the save again when we're back)
        if (!leaguerun::active(id)) leaguerun::begin(id, freshSeed(now));
        std::string why;
        if (leaguerun::gameSaved(id)) {
            if (!resume(app, d_, &why)) toast_.show(why, now);
            return;
        }
        const uint32_t seed = leaguerun::draftSeed(id, freshSeed(now));
        if (d_->factionDraft || leaguerun::keptFaction(id) < 0) {
            app.push(std::unique_ptr<Scene>(new DraftScene(d_, !d_->factionDraft, seed)));
            return;
        }
        leaguerun::Table t = leaguerun::planTable(d_, seed);
        leaguerun::dealFactions(d_, t, leaguerun::keptFaction(id), seed);
        if (!launch(app, d_, t, -1, freshSeed(now), &why)) toast_.show(why, now);
    }
    const league::LeagueDef* d_;
    int sel_ = 0;
    bool confirm_ = false, saved_ = false;
    uint32_t checkedAt_ = 0;
    Toast toast_;
};

// ------------------------------------------------------------------ the ladder
class TournamentScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t now) override {
        rows_ = ladderFromProfile();
        const int n = int(rows_.size());
        const bool map = app.settings().getInt("league_layout", 0) == 1;
        int s = sel_;
        if (in.pressed(Btn::Up) || (map && in.pressed(Btn::Left))) s = (s + n - 1) % n;
        if (in.pressed(Btn::Down) || (map && in.pressed(Btn::Right))) s = (s + 1) % n;
        if (s != sel_) sel_ = s, audio::play(audio::Sfx::Toggle);
        if (in.pressed(Btn::Y)) {  // the list or the map of the leagues (owner: "do both")
            app.settings().setInt("league_layout", map ? 0 : 1);
            app.settings().save();
            audio::play(audio::Sfx::Toggle);
        }
        const LadderEntry& e = rows_[size_t(sel_)];
        if (in.pressed(Btn::X)) app.push(std::unique_ptr<Scene>(new RivalsScene(e.def)));
        if (in.pressed(Btn::A)) {
            if (e.state == kLocked) {
                toast_.show(std::string("Locked: ") + (e.def->unlockText && *e.def->unlockText ? e.def->unlockText : "not open yet"), now);
            } else if (e.def->builder) {
                toast_.show("The Custom League (Tournament Builder) comes in Version 1.1.", now);
            } else {
                audio::play(audio::Sfx::Confirm);
                app.push(std::unique_ptr<Scene>(new HubScene(e.def)));
            }
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return true; }
    void draw(App& app, Canvas& c, uint32_t now) override {
        if (rows_.empty()) rows_ = ladderFromProfile();
        pcolours::setNow(now);
        drawBackdrop(c, now);
        setPickTarget(&sel_);
        if (app.settings().getInt("league_layout", 0) == 1) drawLadderMap(c, rows_, sel_, now);
        else drawLadder(c, rows_, sel_, now);
        toast_.draw(c, now);
    }

private:
    std::vector<LadderEntry> rows_;
    int sel_ = 0;
    Toast toast_;
};

}  // namespace

std::unique_ptr<Scene> makeTournamentScene() { return std::unique_ptr<Scene>(new TournamentScene()); }
