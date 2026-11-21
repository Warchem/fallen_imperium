// The War Sun scene on its own (0.73.0): `ti4 --warsun DIR` makes the art (and says how long it took), saves frames at its
// moments as DIR/warsun_<ms>.ppm and times a frame. TI4_SCALE / TI4_SCREENW for the PC sizes (2.25 and 853: a 1080p monitor).
// TI4_FRAMES_PIPE="ffmpeg ..." (not on Windows): every TI4_PREVIEW_MS (40) ms of the scene as raw RGB frames into that command
// instead - the preview video.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/settings.h"
#include "engine/data.h"
#include "engine/state.h"
#include "game/session.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/platform.h"
#include "ui/theme.h"
#include "ui/warsun_scene.h"

namespace {
void savePpm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.pw(), c.ph());
    std::string row(size_t(c.pw()) * 3u, '\0');
    for (int y = 0; y < c.ph(); ++y) {
        const uint32_t* p = c.pixels() + size_t(y) * size_t(c.pw());
        for (int x = 0; x < c.pw(); ++x) {
            row[size_t(x) * 3] = char((p[x] >> 16) & 255);
            row[size_t(x) * 3 + 1] = char((p[x] >> 8) & 255);
            row[size_t(x) * 3 + 2] = char(p[x] & 255);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}
// as the game draws it: the scene, who found it, and (held) the Continue button
void frame(Canvas& c, uint32_t t) {
    warsun::draw(c, t);
    warsun::drawCaption(c, t, 5, "The Barony of Letnev researched War Sun", rgb(255, 120, 110));
    if (t >= warsun::kContinueAt) {
        const int a = std::min(255, int(t - warsun::kContinueAt) * 255 / 400);
        const int bw = 120, bx = c.fullWidth() - bw - 10, by = 480 - warsun::barHeight(t) + 1;
        const int kw = std::max(18, textWidth(FONT_SMB, "SPACE") + 8), tw = textWidth(FONT_SMB, "Continue");
        const int x0 = bx + (bw - (kw + 6 + tw)) / 2;
        c.fillRoundRect(x0, by + 3, kw, 18, 9, rgb(230, 232, 240), a);
        drawTextAligned(c, FONT_SMB, x0, by + 4, kw, Align::Center, "SPACE", rgb(20, 20, 30), a);
        drawText(c, FONT_SMB, x0 + kw + 6, by + 4, "Continue", rgb(230, 232, 240), a);
    }
}
}  // namespace

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

namespace {
// TI4_WARSUN_GAME=SEED: a watched 6-player game on the real game screen until a computer player gains the first war sun
// technology; then the scene as the game shows it (shots), A pressed in the middle of it, and the game going on after.
// what to wait for: 0 the first war sun technology, 1 the Fracture appearing, 2 the Firmament becoming the Obsidian
int watchedGame(const std::string& outDir, uint32_t seed, float scale, int W, int what) {
    const bool fracture = what != 0;
    std::unique_ptr<Platform> plat = makeHeadlessPlatform("", outDir);
    Settings settings;
    settings.setInt("ai_speed", 1);
    App app(*plat, settings);
    GameSession::savePath() = outDir + "/warsun_save.ti4";
    std::unique_ptr<GameSession> session(new GameSession());
    ti::Setup s;
    s.players = 6;
    s.human = -1;
    s.seed = seed;
    if (fracture) s.modules = ti::kModCodex | ti::kModThundersEdge;  // (0.73.5: a Thunder's Edge game, until the Fracture appears)
    if (what == 2) {  // (0.73.8: with the Firmament in it, until they become the Obsidian)
        s.factions.assign(6, -1);
        for (int f = 0; f < ti::kFactionCount; ++f)
            if (!std::strcmp(ti::kFactions[f].key, "firmament")) s.factions[size_t(seed % 6)] = f;
    }
    if (!session->startNew(s)) return 1;
    GameSession* sp = session.get();
    std::unique_ptr<Scene> scene = makeGameScene(std::move(session));
    Input in;
    Canvas c(W, 480, scale);
    uint32_t t = 1000, found = 0;
    size_t seen = 0;
    auto step = [&](bool press) {
        in.beginFrame();
        in.update(t);
        if (press) in.handle(Btn::A, true, t);
        scene->update(app, in, t);
        if (press) {
            in.beginFrame();
            in.handle(Btn::A, false, t + 1);
        }
        t += 33;
    };
    // (TI4_FRAMES_PIPE + TI4_PIPE_FROM=ms, the Fracture's preview: the game screen from that moment as raw RGB frames every
    // 33 ms into the command, until 4.2 s after the Fracture - the game is the same every run, so a first run finds the moment)
    FILE* pipe = nullptr;
    uint32_t pipeFrom = 0;
    std::string buf;
#ifndef _WIN32
    if (fracture && std::getenv("TI4_FRAMES_PIPE") && std::getenv("TI4_PIPE_FROM")) {
        pipe = popen(std::getenv("TI4_FRAMES_PIPE"), "w");
        pipeFrom = uint32_t(std::atoi(std::getenv("TI4_PIPE_FROM")));
        buf.assign(size_t(c.pw()) * size_t(c.ph()) * 3u, '\0');
    }
#endif
    auto record = [&]() {
        if (!pipe || t < pipeFrom) return;
        scene->draw(app, c, t);
        const uint32_t* px = c.pixels();
        for (size_t i = 0, e = size_t(c.pw()) * size_t(c.ph()); i < e; ++i) {
            buf[i * 3] = char((px[i] >> 16) & 255);
            buf[i * 3 + 1] = char((px[i] >> 8) & 255);
            buf[i * 3 + 2] = char(px[i] & 255);
        }
        std::fwrite(buf.data(), 1, buf.size(), pipe);
    };
    for (; t < 1000u + 90u * 60u * 1000u && !found; ) {
        step(false);
        record();
        const auto& feed = sp->feed();
        for (size_t i = feed.size() > 40 ? feed.size() - 40 : 0; i < feed.size(); ++i)
            if (feed[i].time >= seen && (what == 2   ? feed[i].kind == ti::EventKind::Ability && feed[i].text.find("cast off their mask") != std::string::npos
                                         : fracture ? feed[i].kind == ti::EventKind::Ability && feed[i].text.find("The Fracture tears") == 0
                                                    : feed[i].kind == ti::EventKind::WarSunAge))
                found = feed[i].time;
        if (!feed.empty()) seen = feed.back().time;
    }
    if (!found) {
        for (const auto& f : sp->feed())
            if (f.text.find("War Sun") != std::string::npos || f.text.find("war sun") != std::string::npos) std::printf("  %s\n", f.text.c_str());
        for (const std::string& l : sp->game().log)
            if (l.find("War Sun") != std::string::npos) std::printf("  log: %s\n", l.c_str());
        for (size_t i = sp->game().log.size() > 8 ? sp->game().log.size() - 8 : 0; i < sp->game().log.size(); ++i) std::printf("  last: %s\n", sp->game().log[i].c_str());
        std::printf("warsun game: no war sun technology / Fracture in 90 minutes (round %d, phase %d, age %d)\n", sp->game().round,
                    int(sp->game().phase), int(sp->game().warSunAge));
        return 1;
    }
    std::printf("warsun game: found at %u ms (%.1f min), round %d (%s)\n", found, (found - 1000) / 60000.0, sp->game().round,
                sp->feed().back().text.c_str());
    if (fracture && pipe) {
        for (const uint32_t until = t + (what == 2 ? 8200u : 4200u); t < until;) {
            step(false);
            record();
        }
#ifndef _WIN32
        pclose(pipe);
#endif
        std::printf("warsun game: the Fracture at %u ms of game time, frames piped from %u\n", found, pipeFrom);
        return 0;
    }
    if (fracture) {  // the ripple: frames of it as the game screen shows it
        const uint32_t start = t;

        static const uint32_t kFrac[] = {33, 300, 700, 1200, 1800, 2500, 3200, 4000};
        static const uint32_t kObs[] = {300, 1500, 2700, 3100, 3600, 4300, 5200, 6200, 7200};
        const uint32_t* shots = what == 2 ? kObs : kFrac;
        const size_t nShots = what == 2 ? 9 : 8;
        const uint32_t until = what == 2 ? 7300u : 4100u;
        size_t next = 0;
        double worst = 0, total = 0;
        int frames = 0;
        while (t - start < until) {
            step(false);
            const uint32_t d0 = monotonicMs();
            scene->draw(app, c, t);
            const double ms = double(monotonicMs() - d0);
            worst = std::max(worst, ms);
            total += ms;
            ++frames;
            if (next < nShots && t - start >= shots[next]) {
                savePpm(c, outDir + (what == 2 ? "/obsidian_" : "/fracture_") + std::to_string(shots[next]) + ".ppm");
                ++next;
            }
        }
        std::printf("warsun game: the Fracture's ripple shot (round %d); the game screen %.1f ms a frame, worst %.0f\n", sp->game().round,
                    total / std::max(1, frames), worst);
        return 0;
    }
    // the scene as the game draws it: frames until its words are up, then A
    const uint32_t start = t;
    const uint32_t shots[] = {200, 1500, 3000, 6000, 9500, 13200, 15400};
    size_t next = 0;
    while (t - start < 15600) {
        step(false);
        scene->draw(app, c, t);
        if (next < sizeof shots / sizeof shots[0] && t - start >= shots[next]) {
            savePpm(c, outDir + "/game_warsun_" + std::to_string(shots[next]) + ".ppm");
            std::printf("warsun game: shot at %u ms, holding %d\n", shots[next], int(sp->holding()));
            ++next;
        }
    }
    const bool heldBefore = sp->holding();
    step(true);
    scene->draw(app, c, t);
    for (int i = 0; i < 30; ++i) {
        step(false);
        scene->draw(app, c, t);
    }
    savePpm(c, outDir + "/game_warsun_after.ppm");
    std::printf("warsun game: held %d, after A held %d (kind %d)\n", int(heldBefore), int(sp->holding()), int(sp->heldKind()));
    return heldBefore ? 0 : 1;
}
}  // namespace

int runWarSunTest(const std::string& outDir) {
    const char* se = std::getenv("TI4_SCALE");
    const float scale = se ? std::max(1.f, float(std::atof(se))) : 1.f;
    Canvas::screenScale() = scale;
    const char* we = std::getenv("TI4_SCREENW");
    const int W = we ? std::max(640, std::min(1200, std::atoi(we))) : 640;
    theme::screenWRef() = W;
    if (const char* g = std::getenv("TI4_WARSUN_GAME")) return watchedGame(outDir, uint32_t(std::atoi(g)), scale, W, 0);
    if (const char* g = std::getenv("TI4_FRACTURE_GAME")) return watchedGame(outDir, uint32_t(std::atoi(g)), scale, W, 1);
    if (const char* g = std::getenv("TI4_OBSIDIAN_GAME")) return watchedGame(outDir, uint32_t(std::atoi(g)), scale, W, 2);
    Canvas c(W, 480, scale);
    const uint32_t t0 = monotonicMs();
    warsun::prepare(c);
    warsun::finish();
    std::printf("warsun: art made in %u ms (%d x %d px)\n", monotonicMs() - t0, c.pw(), c.ph());
    if (!warsun::ready(c)) {
        std::printf("warsun: not ready\n");
        return 1;
    }
#ifndef _WIN32
    if (const char* pipeCmd = std::getenv("TI4_FRAMES_PIPE")) {
        const uint32_t step = std::getenv("TI4_PREVIEW_MS") ? uint32_t(std::atoi(std::getenv("TI4_PREVIEW_MS"))) : 40u;
        const uint32_t extra = std::getenv("TI4_WARSUN_EXTRA") ? uint32_t(std::atoi(std::getenv("TI4_WARSUN_EXTRA"))) : 2000u;
        FILE* p = popen(pipeCmd, "w");
        if (!p) return 1;
        std::string buf(size_t(c.pw()) * size_t(c.ph()) * 3u, '\0');
        int n = 0;
        for (uint32_t t = 0; t <= warsun::kLength + extra; t += step) {
            frame(c, t);
            const uint32_t* px = c.pixels();
            for (size_t i = 0, e = size_t(c.pw()) * size_t(c.ph()); i < e; ++i) {
                buf[i * 3] = char((px[i] >> 16) & 255);
                buf[i * 3 + 1] = char((px[i] >> 8) & 255);
                buf[i * 3 + 2] = char(px[i] & 255);
            }
            std::fwrite(buf.data(), 1, buf.size(), p);
            ++n;
        }
        pclose(p);
        std::printf("warsun: %d frames piped\n", n);
        return 0;
    }
#endif
    const char* only = std::getenv("TI4_WARSUN_AT");
    std::vector<uint32_t> shots = {300, 900, 1500, 2300, 3000, 3600, 4300, 6000, 7600, 9200, 10600, 12300, 13600, 15000, 17000, 19000};
    if (only) {
        shots.clear();
        for (const char* s = only; *s;) {
            shots.push_back(uint32_t(std::atoi(s)));
            while (*s && *s != ',') ++s;
            if (*s == ',') ++s;
        }
    }
    for (uint32_t t : shots) {
        frame(c, t);
        savePpm(c, outDir + "/warsun_" + std::to_string(t) + ".ppm");
    }
    // a frame's time: during the pan and at the end
    for (int pass = 0; pass < 2; ++pass) {
        double worst = 0, total = 0;
        for (int f = 0; f < 40; ++f) {
            const uint32_t a = monotonicMs();
            frame(c, (pass ? 16000u : 8000u) + uint32_t(f) * 16u);
            const double ms = double(monotonicMs() - a);
            worst = std::max(worst, ms);
            total += ms;
        }
        std::printf("warsun %s: %.1f ms a frame, worst %.0f\n", pass ? "end" : "panning", total / 40.0, worst);
    }
    return 0;
}
