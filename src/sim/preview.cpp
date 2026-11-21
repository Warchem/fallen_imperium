// ti4 --preview OUTDIR : records short clips of the real game screen for the
// owner (frames as OUTDIR/<clip>/fNNNN.ppm, 10 per second of game time;
// tools/make_previews.sh turns them into MP4 / GIF):
//   map          the galaxy map while the computer players act (a combat included)
//   exploration  you take a planet and read the exploration card
//   negotiations the faction web in the menu, then an offer made and answered
//   faction      the faction sheets and faction cards
//   betrayal     a computer player turns on another (banner), then its attack
//   navigate     looking round the map (R2, D-pad, zoom)
//   tile         the expanded tile view (R2, A, D-pad)
//   offer        (0.57.0) a computer player's offer over the comms codec, accepted, then the comms strip
//   codecbetrayal (0.57.0) a betrayal on the red codec: the betrayer's words, then the answer
//   tilesets     (0.57.2) the same board in Retrowave, Voidfall and Simplistic: the map, zoomed in, a tile up close
// TI4_SCALE=2 records the sharp PC picture; TI4_PREVIEW_MS=50 records 20 frames a second (default 100).
// TI4_PREVIEW_ONLY=betrayal,navigate,... records only those (test only);
// TI4_RETRO=1 records in the Retrowave tileset.
// Inputs are pressed as a player would; your own other decisions are answered
// by the computer so the game moves on.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <sys/stat.h>

#include "ai/ai.h"
#include "app/app.h"
#include "core/settings.h"
#include "engine/deal.h"
#include "engine/engine.h"
#include "engine/map.h"
#include "engine/query.h"
#include "game/session.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/platform.h"
#include "ui/deal_ui.h"
#include "ui/retro.h"
#include "ui/theme.h"

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

namespace {

void makeDir(const std::string& d) {
#ifdef _WIN32
    mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

void savePpm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.pw(), c.ph());
    std::vector<unsigned char> row(size_t(c.pw()) * 3);
    for (int y = 0; y < c.ph(); ++y) {
        for (int x = 0; x < c.pw(); ++x) {
            uint32_t p = c.pixels()[y * c.pw() + x];
            row[size_t(x) * 3] = uint8_t(p >> 16);
            row[size_t(x) * 3 + 1] = uint8_t(p >> 8);
            row[size_t(x) * 3 + 2] = uint8_t(p);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

// Drives one game screen and records frames.
struct Recorder {
    std::unique_ptr<Platform> plat;
    Settings settings;
    std::unique_ptr<App> app;
    GameSession* sp = nullptr;
    std::unique_ptr<Scene> scene;
    Input in;
    // (TI4_SCREENW=853: the 16:9 PC picture - the game screen is laid out for that width)
    static int screenW() {
        const char* e = std::getenv("TI4_SCREENW");
        const int w = e ? std::max(640, std::min(1120, std::atoi(e))) : 640;
        theme::screenWRef() = w;
        return w;
    }
    Canvas c{screenW(), 480, std::getenv("TI4_SCALE") ? float(std::atof(std::getenv("TI4_SCALE"))) : 1.f};
    const uint32_t shotMs = std::getenv("TI4_PREVIEW_MS") ? uint32_t(std::atoi(std::getenv("TI4_PREVIEW_MS"))) : 100U;
    uint32_t t = 1000;
    std::string dir;
    bool rec = false;
    uint32_t nextShot = 0;
    int frames = 0;
    ti::Rng rng{7};
    std::deque<Canvas> ring;  // the last few seconds, while waiting for something to happen
    std::string caption, subCaption;  // (0.57.2: the tileset slideshow's titles, drawn over the frames)
    size_t ringMax = 0;

    bool start(const ti::Setup& s, const std::string& outDir) {
        plat = makeHeadlessPlatform("", outDir);
        settings.setInt("ai_speed", 3);
        settings.setInt("card_pause", 0);
        app.reset(new App(*plat, settings));
        GameSession::savePath() = outDir + "/preview_save.ti4";
        std::unique_ptr<GameSession> session(new GameSession());
        if (!session->startNew(s)) return false;
        sp = session.get();
        scene = makeGameScene(std::move(session));
        return true;
    }
    void speed(int s) { settings.setInt("ai_speed", s); }
    uint32_t firstFrameT = 0;  // the time of the first frame saved (the lead-in included)
    void record(const std::string& d) {
        dir = d;
        makeDir(dir);
        rec = true;
        firstFrameT = nextShot - shotMs * uint32_t(ring.size());
        nextShot = t;
        frames = 0;
        // the lead-in kept in the ring first
        for (const Canvas& f : ring) savePpm(f, dir + "/f" + num(++frames) + ".ppm");
        ring.clear();
        ringMax = 0;
    }
    void stop() { rec = false; }
    void resume() {  // record again into the same clip (lead-in ring first)
        rec = true;
        nextShot = t;
        for (const Canvas& f : ring) savePpm(f, dir + "/f" + num(++frames) + ".ppm");
        ring.clear();
        ringMax = 0;
    }
    static std::string num(int n) {
        char b[16];
        std::snprintf(b, sizeof b, "%04d", n);
        return b;
    }
    void frame(Btn down = Btn::None, Btn up = Btn::None) {
        in.beginFrame();
        in.update(t);
        if (down != Btn::None) in.handle(down, true, t);
        if (up != Btn::None) in.handle(up, false, t);
        scene->update(*app, in, t);
        if ((rec || ringMax > 0) && int32_t(t - nextShot) >= 0) {
            scene->draw(*app, c, t);
            if (!caption.empty()) {
                c.fillRect(0, 30, c.width(), 52, rgb(6, 6, 16), 190);
                drawTextAligned(c, FONT_XL, 2, 34, c.width(), Align::Center, caption, rgb(0, 0, 0), 200);
                drawTextAligned(c, FONT_XL, 0, 32, c.width(), Align::Center, caption, rgb(255, 120, 220));
                drawTextAligned(c, FONT_SM, 0, 64, c.width(), Align::Center, subCaption, rgb(220, 226, 245));
            }
            if (rec) {
                savePpm(c, dir + "/f" + num(++frames) + ".ppm");
            } else {
                ring.push_back(c);
                while (ring.size() > ringMax) ring.pop_front();
            }
            nextShot += shotMs;
        }
        t += 33;
    }
    void idle(int ms) {
        for (int k = 0; k < ms / 33; ++k) frame();
    }
    void press(Btn b, int afterMs = 400) {
        frame(b);
        frame(Btn::None, b);
        idle(afterMs);
    }
    // the computer answers one of your decisions (it shows for 'showMs' first);
    // editors (moving, landing, paying...) are filled with Y and confirmed with START like a player would
    bool autoAnswer(int showMs = 0) {
        if (!sp->humanToAnswer()) return false;
        if (showMs > 0) idle(showMs);
        if (!sp->humanToAnswer()) return false;
        const ti::DecisionKind k = sp->engine().decision().kind;
        if (showMs > 0 && (k == ti::DecisionKind::Move || k == ti::DecisionKind::Land || k == ti::DecisionKind::Produce ||
                           k == ti::DecisionKind::Pay)) {
            const int before = sp->engine().answerCount();
            press(Btn::Y, showMs);
            press(Btn::Start, 300);
            if (sp->engine().answerCount() != before) return true;  // the screen took it
        }
        ti::Answer a = ti::aiAnswer(sp->game(), sp->engine().decision(), rng);
        std::string err;
        sp->submit(a, &err, t);
        frame();
        return true;
    }
    // let the game run (cards held for A are released after 'cardMs')
    uint32_t holdSince = 0;
    void run(int ms, int cardMs = 1200) {
        uint32_t end = t + uint32_t(ms);
        while (int32_t(t - end) < 0 && !sp->over()) {
            if (sp->holding()) {
                if (!holdSince) holdSince = t;
                if (t - holdSince > uint32_t(cardMs)) {
                    press(Btn::A, 0);
                    holdSince = 0;
                    continue;
                }
            } else {
                holdSince = 0;
            }
            if (sp->humanToAnswer()) autoAnswer(300);
            else frame();
        }
    }
};

// ---------------------------------------------------------------- the map in action
int clipMap(const std::string& out) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.autoplay = true;  // watching: the computer plays every seat
    s.seed = 4242;
    if (!r.start(s, out)) return 0;
    // fast forward to round 3
    while (!r.sp->over() && r.sp->game().round < 3) r.run(2000, 0);
    // then watch at normal speed, keeping the last 4 seconds, until a space combat starts
    r.speed(1);
    r.settings.setInt("card_pause", 2);  // exploration cards show briefly instead of waiting for A
    r.ringMax = 40;
    r.nextShot = r.t;
    uint32_t lastTime = r.t;
    bool combat = false;
    for (int guard = 0; guard < 40000 && !combat && !r.sp->over(); ++guard) {
        r.run(100);
        for (const FeedLine& f : r.sp->feed())  // new lines by their timestamps (the feed is capped)
            if (f.time >= lastTime && f.kind == ti::EventKind::CombatStart && f.text.find("Space combat") != std::string::npos)
                combat = true;
        lastTime = r.t;
    }
    r.record(out + "/map");
    r.run(13000, 1500);
    r.stop();
    std::printf("preview: map %d frames (round %d)\n", r.frames, r.sp->game().round);
    return r.frames;
}

// ---------------------------------------------------------------- an exploration card
int clipExploration(const std::string& out) {
    Recorder r;
    ti::Setup s;
    s.players = 5;
    s.human = std::max(0, ti::southernmostSeat(5));
    s.seed = 777;
    if (!r.start(s, out)) return 0;
    const int me = s.human;
    for (int guard = 0; guard < 200000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            r.press(Btn::A, 0);
            continue;
        }
        if (!r.sp->humanToAnswer()) {
            r.frame();
            continue;
        }
        const ti::Decision& d = r.sp->engine().decision();
        // our tactical action into a system with planets nobody has explored
        if (d.tag == ti::Tag::Activate && r.sp->game().round >= 1) {
            ti::Answer a = ti::aiAnswer(r.sp->game(), d, r.rng);
            int sys = a.picks.empty() ? -1 : a.picks[0];
            bool fresh = false;
            if (sys >= 0)
                for (int16_t x : r.sp->game().systems[sys].planets)
                    if (r.sp->game().planets[x].owner < 0 && ti::kPlanets[r.sp->game().planets[x].def].trait != ti::Trait::None)
                        fresh = true;
            if (fresh) {
                r.speed(1);
                r.record(out + "/exploration");
                r.idle(900);
                std::string err;
                r.sp->submit(a, &err, r.t);
                bool done = false;
                for (int k = 0; k < 4000 && !done && !r.sp->over(); ++k) {
                    if (r.sp->holding() && r.sp->heldKind() == ti::EventKind::Explore) {
                        r.idle(2600);  // reading the card
                        r.press(Btn::Right, 500);  // looking at the map around it while the card waits
                        r.press(Btn::Up, 700);
                        r.idle(800);
                        r.press(Btn::A, 1600);
                        done = true;
                        break;
                    }
                    if (r.sp->holding()) {
                        r.press(Btn::A, 300);
                        continue;
                    }
                    if (r.sp->humanToAnswer()) {
                        if (r.sp->engine().decision().player != me) break;
                        r.autoAnswer(700);
                        if (r.sp->game().activePlayer != me) break;
                    } else {
                        r.frame();
                    }
                }
                r.stop();
                if (done) {
                    std::printf("preview: exploration %d frames (round %d)\n", r.frames, r.sp->game().round);
                    return r.frames;
                }
                r.speed(3);  // no card this time: try again later
                continue;
            }
        }
        std::string err;
        r.sp->submit(ti::aiAnswer(r.sp->game(), d, r.rng), &err, r.t);
        r.frame();
    }
    return 0;
}

// ---------------------------------------------------------------- negotiations and faction sheets
int clipNegotiations(const std::string& out, bool faction, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    const int me = s.human;
    for (int guard = 0; guard < 400000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            r.press(Btn::A, 0);
            continue;
        }
        if (!r.sp->humanToAnswer()) {
            r.frame();
            continue;
        }
        const ti::Game& g = r.sp->game();
        const ti::Decision& d = r.sp->engine().decision();
        // round 4 (archetypes revealed), choosing an action, with a neighbour to make a good offer to
        bool ready = false;
        int negIndex = -1;
        if (d.tag == ti::Tag::ActionChoice && g.round >= 4 && g.archRevealed) {
            for (size_t i = 0; i < d.options.size(); ++i)
                if (d.options[i].id == 650 && d.options[i].enabled) negIndex = int(i);
            if (faction) ready = true;
            // the partner the "Negotiate" web will start on (as NegotiateUI picks it) must have a deal to suggest
            if (negIndex >= 0 && !ready) {
                FactionWeb web = factionWebLayout(g, Rect{4, 70, 410, 362}, me);
                int sel = -1;
                float best = 1e9f;
                for (int q = 0; q < g.numPlayers; ++q) {
                    if (q == me || g.players[q].eliminated || !ti::canOffer(g, me, q)) continue;
                    float dd = std::fabs(web.x[q] - web.cx) + std::fabs(web.y[q] - (web.cy + 200)) +
                               (ti::transactionAllowed(g, me, q) ? 0.f : 2000.f);
                    if (dd < best) {
                        best = dd;
                        sel = q;
                    }
                }
                ti::Deal deal;
                if (sel >= 0 && ti::transactionAllowed(g, me, sel) && ti::aiSuggestDeal(g, me, sel, nullptr, deal) && deal.contract())
                    ready = true;
            }
        }
        if (!ready) {
            std::string err;
            r.sp->submit(ti::aiAnswer(g, d, r.rng), &err, r.t);
            r.frame();
            continue;
        }
        r.speed(1);
        r.idle(300);
        if (faction) {
            r.record(out + "/faction");
            r.idle(800);
            r.press(Btn::Menu, 900);
            r.press(Btn::Down, 300);
            r.press(Btn::Down, 700);
            r.press(Btn::A, 2200);       // your faction sheet
            r.press(Btn::Right, 1800);   // the next player's
            r.press(Btn::Right, 1800);
            r.press(Btn::A, 1600);       // their faction cards
            r.press(Btn::Right, 700);
            r.press(Btn::Right, 700);
            r.press(Btn::Down, 900);
            r.press(Btn::A, 2600);       // one card in full
            r.press(Btn::B, 600);
            r.press(Btn::B, 900);
            r.press(Btn::Left, 1200);
            r.press(Btn::Left, 1500);
            r.press(Btn::B, 600);
            r.stop();
            std::printf("preview: faction %d frames (round %d)\n", r.frames, g.round);
            return r.frames;
        }
        r.record(out + "/negotiations");
        r.idle(1200);                     // the action list
        r.press(Btn::Menu, 1300);         // the menu: faction web + list
        r.press(Btn::Down, 700);          // Negotiations
        r.press(Btn::A, 1600);            // pick factions on the web
        r.press(Btn::Right, 1500);
        r.press(Btn::Up, 1500);
        r.press(Btn::B, 600);
        r.press(Btn::B, 900);             // back to the game
        for (int k = 0; k < negIndex; ++k) r.press(Btn::Down, 220);
        r.idle(600);
        r.press(Btn::A, 1600);            // Negotiate: choose a partner on the web
        r.press(Btn::A, 1300);            // make an offer
        r.press(Btn::Y, 1800);            // suggested deal
        r.press(Btn::Down, 450);
        r.press(Btn::Down, 900);
        r.press(Btn::Start, 600);         // send it
        r.run(5000, 2000);                // their answer and the banner
        r.stop();
        std::printf("preview: negotiations %d frames (round %d)\n", r.frames, g.round);
        return r.frames;
    }
    return 0;
}

// ---------------------------------------------------------------- a betrayal, then the attack
int clipBetrayal(const std::string& out, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.autoplay = true;
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    // fast forward frame by frame (at Instant speed a whole game fits in a few frames)
    while (!r.sp->over() && r.sp->game().round < 3) r.frame();
    r.speed(1);
    r.settings.setInt("card_pause", 2);
    r.ringMax = 25;
    r.nextShot = r.t;
    uint32_t last = r.t;
    int bot = -1;
    for (int guard = 0; guard < 60000 && bot < 0 && !r.sp->over(); ++guard) {
        r.run(100);
        for (const FeedLine& f : r.sp->feed())
            if (f.time >= last && f.kind == ti::EventKind::Betrayal) {
                if (std::getenv("TI4_BETRAYDEBUG")) std::fprintf(stderr, "feed: %s (system %d)\n", f.text.c_str(), f.system);
                if (f.system >= 0) bot = f.player;
            }
        last = r.t;
    }
    if (std::getenv("TI4_BETRAYDEBUG"))
        for (const FeedLine& f : r.sp->feed())
            if (f.kind == ti::EventKind::Betrayal || f.text.find("offended") != std::string::npos)
                std::fprintf(stderr, "end feed: kind %d time %u last %u: %s\n", int(f.kind), f.time, last, f.text.c_str());
    std::fprintf(stderr, "betrayal seed %u: round %d over %d bot %d\n", unsigned(seed), r.sp->game().round, int(r.sp->over()), bot);
    if (bot < 0) return 0;
    r.record(out + "/betrayal");
    r.run(8000, 3000);
    r.stop();
    // then wait for the betrayer's next fight
    r.ringMax = 30;
    r.nextShot = r.t;
    last = r.t;
    const int until = r.sp->game().round + 2;
    bool fight = false;
    for (int guard = 0; guard < 60000 && !fight && !r.sp->over() && r.sp->game().round <= until; ++guard) {
        r.run(100);
        for (const FeedLine& f : r.sp->feed())
            if (f.time >= last && f.kind == ti::EventKind::CombatStart && f.player == bot) fight = true;
        last = r.t;
    }
    if (!fight) return 0;
    r.resume();
    r.run(16000, 2500);
    r.stop();
    std::printf("preview: betrayal %d frames (round %d, seed %u)\n", r.frames, r.sp->game().round, unsigned(seed));
    return r.frames;
}

// ---------------------------------------------------------------- 0.57.0: a computer player's offer over the codec
int clipCodecOffer(const std::string& out, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    for (int guard = 0; guard < 400000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            r.press(Btn::A, 0);
            continue;
        }
        if (!r.sp->humanToAnswer()) {
            r.frame();
            continue;
        }
        const ti::Game& g = r.sp->game();
        const ti::Decision& d = r.sp->engine().decision();
        const int from = d.targetPlayer;
        if (d.tag == ti::Tag::DealResponse && g.round >= 2 && from >= 0 && !g.players[size_t(from)].human) {
            r.speed(1);
            r.ringMax = 12;
            r.idle(400);
            r.record(out + "/offer");
            r.idle(9500);  // the codec switches on, the words type out
            int accept = 0;
            for (size_t i = 0; i < d.options.size(); ++i)
                if (d.options[i].id == 1) accept = int(i);
            const int n = int(d.options.size());
            r.press(Btn::Right, 700);
            r.press(Btn::Right, 700);
            for (int k = (accept - 2 % n + 2 * n) % n; k > 0; --k) r.press(Btn::Right, 700);  // on round to Accept
            r.idle(500);
            r.press(Btn::A, 0);
            r.run(4000, 1500);  // the deal banner: their words
            r.stop();
            std::printf("preview: offer %d frames (round %d)\n", r.frames, g.round);
            return r.frames;
        }
        std::string err;
        r.sp->submit(ti::aiAnswer(g, d, r.rng), &err, r.t);
        r.frame();
    }
    return 0;
}

// ---------------------------------------------------------------- 0.57.0: a betrayal on the red codec
int clipCodecBetrayal(const std::string& out, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.autoplay = true;
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    r.settings.setInt("card_pause", 2);
    while (!r.sp->over() && r.sp->game().round < 3) r.frame();
    r.speed(1);
    r.settings.setInt("card_pause", 0);  // betrayals wait for A
    r.ringMax = 30;
    r.nextShot = r.t;
    for (int guard = 0; guard < 200000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            if (r.sp->heldKind() == ti::EventKind::Betrayal) {
                r.record(out + "/codecbetrayal");
                r.idle(9800);  // both lines type out
                r.press(Btn::A, 1500);
                r.stop();
                std::printf("preview: codec betrayal %d frames (round %d, seed %u)\n", r.frames, r.sp->game().round, unsigned(seed));
                return r.frames;
            }
            r.press(Btn::A, 0);
            continue;
        }
        r.frame();
    }
    return 0;
}

// ---------------------------------------------------------------- 0.57.2: the three tilesets on the same board
int clipTilesets(const std::string& out, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    for (int guard = 0; guard < 400000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            r.press(Btn::A, 0);
            continue;
        }
        if (!r.sp->humanToAnswer()) {
            r.frame();
            continue;
        }
        const ti::Decision& d = r.sp->engine().decision();
        if (d.tag == ti::Tag::ActionChoice && r.sp->game().round >= 3) break;
        std::string err;
        r.sp->submit(ti::aiAnswer(r.sp->game(), d, r.rng), &err, r.t);
        r.frame();
    }
    if (r.sp->over()) return 0;
    r.speed(1);
    r.idle(400);
    struct Look {
        int ts;
        const char* name;
        const char* sub;
    };
    const Look looks[] = {{retro::kRetrowave, "RETROWAVE", "Animated pixel art - the default"},
                          {retro::kVoidfall, "VOIDFALL", "Painted, high definition, animated"},
                          {retro::kSimplistic, "SIMPLISTIC", "Clean drawn shapes - the original look"}};
    bool first = true;
    for (const Look& l : looks) {
        if (!retro::tilesetAvailable(l.ts)) continue;
        retro::setTileset(l.ts);
        r.caption = l.name;
        r.subCaption = l.sub;
        if (first) {
            r.record(out + "/tilesets");
            r.press(Btn::R2, 1600);  // the whole board
            first = false;
        } else {
            r.idle(1800);
        }
        r.caption.clear();
        r.idle(700);
        r.press(Btn::R1, 1000);  // closer
        r.press(Btn::Up, 600);
        r.press(Btn::Right, 700);
        r.press(Btn::R1, 1400);
        r.press(Btn::A, 2600);   // one tile up close
        r.press(Btn::B, 700);
        r.press(Btn::L1, 700);   // back out
        r.press(Btn::L1, 1100);
        r.press(Btn::Left, 300);
        r.press(Btn::Down, 300);
    }
    r.press(Btn::R2, 600);
    r.stop();
    retro::setTileset(retro::kRetrowave);
    std::printf("preview: tilesets %d frames (round %d)\n", r.frames, r.sp->game().round);
    return r.frames;
}

// ---------------------------------------------------------------- 0.71.0: combat reactions
// A watched game until a space (or ground) combat that lasts long enough for both lines; run with TI4_TALK_EVERY=1 (every
// fight talks) and TI4_SCREENW=853 TI4_SCALE=2.25 for the 16:9 PC picture. Prints the record start time; with
// TI4_TALKLOG=1 the scene prints the lines and the voices' times (for the sound track).
int clipCombatTalk(const std::string& out, uint32_t seed, bool ground) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.autoplay = true;
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    while (!r.sp->over() && r.sp->game().round < 2) r.frame();
    r.speed(1);
    r.settings.setInt("card_pause", 2);
    const std::string dir = out + (ground ? "/talk_ground" : "/talk_space");
    const char* want = ground ? "Ground combat on " : "Space combat in ";
    const uint32_t minLen = std::getenv("TI4_TALK_MINMS") ? uint32_t(std::atoi(std::getenv("TI4_TALK_MINMS"))) : 9500U;
    for (int tries = 0; tries < 40 && !r.sp->over() && r.sp->game().round <= 6; ++tries) {
        r.ringMax = std::max<size_t>(12, 1500 / std::max(1U, r.shotMs));  // 1.5 s of lead-in
        r.nextShot = r.t;
        uint32_t last = r.t;
        bool found = false;
        uint32_t startT = 0;
        for (int guard = 0; guard < 60000 && !found && !r.sp->over(); ++guard) {
            r.run(100);
            for (const FeedLine& f : r.sp->feed())
                if (f.time >= last && f.kind == ti::EventKind::CombatStart && f.text.compare(0, std::strlen(want), want) == 0) {
                    found = true;
                    startT = f.time;
                }
            last = r.t;
        }
        if (!found) return 0;
        std::remove(dir.c_str());
        r.record(dir);
        std::fprintf(stderr, "combattalk record start %u (combat %u)\n", unsigned(r.firstFrameT), unsigned(startT));
        r.run(int(minLen) + 3000, 1500);
        r.stop();
        uint32_t endT = 0;
        for (const FeedLine& f : r.sp->feed())
            if (f.time >= startT && f.kind == ti::EventKind::CombatEnd && !endT) endT = f.time;
        std::fprintf(stderr, "combattalk try %d: combat %u..%u (%d ms), %d frames\n", tries, unsigned(startT), unsigned(endT),
                     endT ? int(endT - startT) : -1, r.frames);
        if (!endT || endT - startT >= minLen) {
            std::printf("preview: %s %d frames (round %d, seed %u)\n", ground ? "talk_ground" : "talk_space", r.frames, r.sp->game().round,
                        unsigned(seed));
            return r.frames;
        }
        for (int k = 1; k <= r.frames; ++k) std::remove((dir + "/f" + Recorder::num(k) + ".ppm").c_str());
    }
    return 0;
}

// ---------------------------------------------------------------- looking round the map / the expanded tile
int clipNavigate(const std::string& out, bool tile, uint32_t seed) {
    Recorder r;
    ti::Setup s;
    s.players = 6;
    s.human = std::max(0, ti::southernmostSeat(6));
    s.seed = seed;
    if (!r.start(s, out)) return 0;
    for (int guard = 0; guard < 400000 && !r.sp->over(); ++guard) {
        if (r.sp->holding()) {
            r.press(Btn::A, 0);
            continue;
        }
        if (!r.sp->humanToAnswer()) {
            r.frame();
            continue;
        }
        const ti::Decision& d = r.sp->engine().decision();
        if (d.tag == ti::Tag::ActionChoice && r.sp->game().round >= 3) break;
        std::string err;
        r.sp->submit(ti::aiAnswer(r.sp->game(), d, r.rng), &err, r.t);
        r.frame();
    }
    if (r.sp->over()) return 0;
    r.speed(1);
    r.idle(400);
    r.record(out + (tile ? "/tile" : "/navigate"));
    r.idle(1000);
    r.press(Btn::R2, 1200);  // look at the map
    if (!tile) {
        const Btn path[] = {Btn::Up, Btn::Up, Btn::Up, Btn::Right, Btn::Right, Btn::Up};
        for (Btn b : path) r.press(b, 450);
        r.press(Btn::R1, 900);  // zoom in
        r.press(Btn::R1, 1100);
        const Btn path2[] = {Btn::Left, Btn::Left, Btn::Left, Btn::Down, Btn::Down};
        for (Btn b : path2) r.press(b, 500);
        r.press(Btn::L1, 800);  // zoom out
        r.press(Btn::L1, 800);
        r.press(Btn::L1, 1300);
        const Btn path3[] = {Btn::Right, Btn::Right, Btn::Down, Btn::Down, Btn::Down};
        for (Btn b : path3) r.press(b, 450);
        r.press(Btn::R1, 1500);
        r.press(Btn::R2, 1000);  // back to the game
    } else {
        r.press(Btn::A, 3200);      // your home system up close
        r.press(Btn::Up, 2600);     // the next tiles
        r.press(Btn::Up, 2600);
        r.press(Btn::Right, 2600);
        r.press(Btn::Up, 3000);
        r.press(Btn::B, 1200);
        r.press(Btn::R2, 800);
    }
    r.stop();
    std::printf("preview: %s %d frames (round %d)\n", tile ? "tile" : "navigate", r.frames, r.sp->game().round);
    return r.frames;
}

}  // namespace

int runPreview(const std::string& outDir) {
    makeDir(outDir);
    if (std::getenv("TI4_RETRO")) retro::setOn(true);
    if (std::getenv("TI4_VOIDFALL")) retro::setTileset(retro::kVoidfall);
    std::string only = std::getenv("TI4_PREVIEW_ONLY") ? std::getenv("TI4_PREVIEW_ONLY") : "";
    auto want = [&](const char* k) { return only.empty() || ("," + only + ",").find("," + std::string(k) + ",") != std::string::npos; };
    bool ok = true;
    if (want("map")) ok = clipMap(outDir) > 0 && ok;
    if (want("exploration")) ok = clipExploration(outDir) > 0 && ok;
    int c = 0, d = 0;
    if (want("negotiations")) {
        for (uint32_t seed = 11; seed < 40 && !c; ++seed) c = clipNegotiations(outDir, false, seed);
        ok = c > 0 && ok;
    }
    if (want("faction")) {
        for (uint32_t seed = 11; seed < 40 && !d; ++seed) d = clipNegotiations(outDir, true, seed);
        ok = d > 0 && ok;
    }
    if (want("betrayal")) {
        int b = 0;
        for (uint32_t seed = 4242; seed < 4262 && !b; ++seed) b = clipBetrayal(outDir, seed);
        ok = b > 0 && ok;
    }
    if (!only.empty() && want("offer")) {  // (0.57.0; only when asked for by name)
        int o = 0;
        for (uint32_t seed = 11; seed < 60 && !o; ++seed) o = clipCodecOffer(outDir, seed);
        ok = o > 0 && ok;
    }
    if (!only.empty() && want("tilesets")) ok = clipTilesets(outDir, 31) > 0 && ok;  // (0.57.2)
    if (!only.empty() && want("codecbetrayal")) {
        int b = 0;
        for (uint32_t seed = 4242; seed < 4262 && !b; ++seed) b = clipCodecBetrayal(outDir, seed);
        ok = b > 0 && ok;
    }
    if (!only.empty() && want("talkspace")) {  // (draft, by name only)
        int t = 0;
        for (uint32_t seed = 4242; seed < 4250 && !t; ++seed) t = clipCombatTalk(outDir, seed, false);
        ok = t > 0 && ok;
    }
    if (!only.empty() && want("talkground")) {
        int t = 0;
        for (uint32_t seed = 4242; seed < 4250 && !t; ++seed) t = clipCombatTalk(outDir, seed, true);
        ok = t > 0 && ok;
    }
    if (want("navigate")) ok = clipNavigate(outDir, false, 31) > 0 && ok;
    if (want("tile")) ok = clipNavigate(outDir, true, 31) > 0 && ok;
    std::remove((outDir + "/preview_save.ti4").c_str());
    return ok ? 0 : 1;
}
