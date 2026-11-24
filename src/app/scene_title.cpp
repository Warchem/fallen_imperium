// Title screen: continue / new game / device tests / quit.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <memory>

#include "game/habits.h"
#include "app/app.h"
#include "app/bug_report.h"
#include "audio/audio.h"
#include "audio/library.h"
#include "ui/backdrops.h"
#include "gfx/picture.h"
#include "core/paths.h"
#include "core/keymap.h"
#include "core/log.h"
#include "platform/platform.h"
#include "game/session.h"
#include "game/profile.h"
#include "app/league_ui.h"
#include "gfx/font.h"
#include "engine/data.h"
#include "ui/hits.h"
#include "ui/icons.h"
#include "ui/map_view.h"
#include "ui/retro.h"
#include "ui/ground_units.h"
#include "ui/theme.h"
#include "ui/title_bg.h"
#include "ui/title_war.h"
#include "ui/widgets.h"

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

namespace {

std::unique_ptr<Scene> makeCampaignScene();  // below (0.30.0)
std::unique_ptr<Scene> makeModulesScene();   // below (0.31.0)
std::unique_ptr<Scene> makeEventsScene();    // below (0.36.0)

// ---------------------------------------------------------------- 0.59.70: the Retrowave and Voidfall titles (owner)
// With the Retrowave tileset the title and its menus stand over the animated planet (ui/title_bg: the nuclear-battered world,
// its one great city, the station and the traffic); with Voidfall over the battlefield (ui/title_war: a neon twilight, ruins,
// the two armies, the ships above); the menus are see-through then, in neon (Retrowave) or ember (Voidfall) colours. The
// Simplistic tileset keeps the Simplistic title.
bool retroTitle() { return retro::tileset() == retro::kRetrowave; }
bool voidTitle() { return retro::tileset() == retro::kVoidfall; }
bool artTitle() { return retroTitle() || voidTitle(); }
void titleBackdrop(Canvas& c, uint32_t now) {
    if (retroTitle()) titlebg::draw(c, now, 1.f, 1.f);
    else if (voidTitle()) titlewar::draw(c, now, 1.f, 1.f);
    else drawSpaceBackground(c, true);
}
struct TitlePal {
    uint32_t fill, glow, frame, top, selFill, selA, selB, marker, label, labelSel, detail, detailSel;
};
const TitlePal& titlePal() {
    static const TitlePal neon{rgb(12, 6, 30),   rgb(255, 60, 200), rgb(255, 80, 210), rgb(110, 230, 255), rgb(92, 22, 98),  rgb(50, 120, 160),
                               rgb(120, 240, 255), rgb(120, 240, 255), rgb(196, 176, 226), rgb(246, 240, 255), rgb(128, 110, 158), rgb(170, 214, 236)};
    static const TitlePal ember{rgb(18, 8, 22),   rgb(255, 110, 50), rgb(255, 140, 70),  rgb(255, 206, 130), rgb(112, 36, 40), rgb(190, 80, 50),
                                rgb(255, 204, 110), rgb(255, 170, 80), rgb(226, 196, 196), rgb(255, 244, 232), rgb(150, 116, 128), rgb(236, 188, 160)};
    return voidTitle() ? ember : neon;
}
// a see-through panel with glowing edges (fade 0..255 for the intro)
void neonPanel(Canvas& c, Rect r, int radius, int fade = 255) {
    const TitlePal& p = titlePal();
    c.fillRoundRect(r.x, r.y, r.w, r.h, radius, p.fill, 168 * fade / 255);
    c.frameRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, radius + 2, p.glow, 1, 50 * fade / 255);
    c.frameRoundRect(r.x, r.y, r.w, r.h, radius, p.frame, 1, 210 * fade / 255);
    c.hline(r.x + radius, r.x + r.w - radius - 1, r.y + 1, p.top, 150 * fade / 255);  // a highlight along the top
}
void titlePanel(Canvas& c, Rect r, int radius, int fade = 255) {
    if (artTitle()) neonPanel(c, r, radius, fade);
    else drawPanel(c, r, theme::panel, theme::border, radius, 235 * fade / 255);
}
// a chosen row on the Modules / Galactic events lists
void selectedRow(Canvas& c, Rect r, int radius) {
    if (artTitle()) {
        c.fillRoundRect(r.x, r.y, r.w, r.h, radius, titlePal().selFill, 150);
        c.frameRoundRect(r.x, r.y, r.w, r.h, radius, titlePal().selB, 1);
    } else {
        c.fillRoundRect(r.x, r.y, r.w, r.h, radius, theme::panelHi, 240);
        c.frameRoundRect(r.x, r.y, r.w, r.h, radius, theme::gold, 1);
    }
}
// drawMenu (widgets) in the neon look, or the usual one; fade < 255 (the intro) draws it see-through and takes no clicks
void titleMenu(Canvas& c, Rect r, const std::vector<MenuItem>& items, int selected, uint32_t now, int* pick, int rowH = 40, int fade = 255) {
    if (!artTitle() && fade >= 255) {
        drawMenu(c, r, items, selected, now, pick, rowH);
        return;
    }
    const bool neon = artTitle();
    const TitlePal& pal = titlePal();
    if (fade < 255) pick = nullptr;
    for (size_t i = 0; i < items.size(); ++i) {
        const int y = r.y + int(i) * rowH;
        const bool sel = int(i) == selected;
        if (pick) hits::add(Rect{r.x, y, r.w, rowH - 4}, [pick, i]() { *pick = int(i); });
        const int boxH = rowH <= 30 ? rowH - 1 : rowH - 6;  // (0.69.0: twelve Options rows - the detail line stays inside)
        if (sel) {
            const float pulse = 0.5f + 0.5f * std::sin(float(now % 2000) / 2000.f * 6.2831853f);
            if (neon) {
                c.fillRoundRect(r.x, y, r.w, boxH, 8, pal.selFill, 150 * fade / 255);
                c.frameRoundRect(r.x, y, r.w, boxH, 8, mix(pal.selA, pal.selB, int(pulse * 255)), 2, fade);
            } else {
                c.fillRoundRect(r.x, y, r.w, boxH, 8, theme::panelHi, 240 * fade / 255);
                c.frameRoundRect(r.x, y, r.w, boxH, 8, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2, fade);
            }
            const float tx = float(r.x + 12), ty = float(y + boxH / 2);
            float xs[3] = {tx, tx + 8, tx}, ys[3] = {ty - 6, ty, ty + 6};
            c.fillPolygon(xs, ys, 3, neon ? pal.marker : theme::gold);
        }
        const int dy = rowH <= 30 ? 1 : rowH < 40 ? 2 : 3, ddy = rowH <= 30 ? 15 : rowH < 40 ? 17 : 20;
        const uint32_t label = neon ? (sel ? pal.labelSel : pal.label) : (sel ? theme::text : theme::dim);
        const uint32_t detail = neon ? (sel ? pal.detailSel : pal.detail) : (sel ? theme::dim : theme::faint);
        drawText(c, FONT_MDB, r.x + 28, y + dy, items[i].label, label, fade);
        if (!items[i].detail.empty()) drawText(c, FONT_XS, r.x + 28, y + ddy, items[i].detail, detail, fade);
        static const std::string kArrows = "\xE2\x97\x80 \xE2\x96\xB6";
        if (sel && pick && items[i].detail.compare(0, kArrows.size(), kArrows) == 0) {
            const int aw = textWidth(FONT_XS, "\xE2\x97\x80 ");
            hits::addButton(Rect{r.x + 22, y + ddy - 3, aw + 2, 16}, Btn::Left);
            hits::addButton(Rect{r.x + 24 + aw, y + ddy - 3, aw + 4, 16}, Btn::Right);
        }
    }
}

// The opening (0.59.70, owner): "when the game starts up, the screen is black with scrolling stars. The text of 'Fallen Imperium'
// appears slowly from left to right with a sound effect, but in Lazaxian font. Trailing the appearance of the word, the font
// changes slowly to English ... The background slowly pans to a highly detailed planet" - and "the title screen music doesn't
// start until the sound effects of the title and its transformation to the English font in its entirety is complete".
// Times in ms from the start (once the sounds are made, at most kWait after the title first shows); any button skips it.
namespace intro {
constexpr uint32_t kWait = 2500;                       // longest wait for the sound effects to be made
constexpr uint32_t kFirst = 700, kStep = 210;          // the Lazaxian letters, one by one from the left
constexpr uint32_t kFade = 280;                        // each letter's fade in
constexpr uint32_t kMorphLag = 1500, kMorph = 900;     // each phases into its English letter this long after it came
constexpr uint32_t kRevealSound = 3450;                // TitleReveal's length (synth.cpp)
// (0.60.0, owner 2026-11-17 14:59: "have it blend to the start of the intro song" - the title track starts from its beginning,
// rising as the reveal's swell dies away: the swell peaks 1.1 s in and has decayed to a third by 2 s)
constexpr uint32_t kMusicBlend = 1900;                 // the music starts this long after the reveal
constexpr uint32_t kMusicFade = 2300;                  // the title track's fade in
constexpr uint32_t kSkyStart = 2600, kSky = 5400;      // the nebula and the sky fade in
constexpr uint32_t kPanStart = 3400, kPan = 6000;      // the slow pan down to the planet
constexpr uint32_t kMenuStart = 9200, kMenu = 1300;    // the rest of the title and the menu fade in
const char* const kName = "FALLEN IMPERIUM";
constexpr int kLetters = 15;
constexpr uint32_t morphEnd() { return kFirst + uint32_t(kLetters - 1) * kStep + kMorphLag + kMorph; }  // the name in English
constexpr uint32_t revealAt() { return morphEnd() + 60; }
constexpr uint32_t musicAt() { return revealAt() + kMusicBlend; }
constexpr uint32_t endAt() { return kMenuStart + kMenu; }
// Voidfall (owner 12:45: "After the font transformation, the screen pans down to an open battlefield"): the pan starts when the
// name is complete, and the menu comes when it is over
constexpr uint32_t kVoidPan = 4200;
constexpr uint32_t voidPanStart() { return morphEnd() + 260; }
constexpr uint32_t voidMenuStart() { return voidPanStart() + kVoidPan - 300; }
constexpr uint32_t voidEndAt() { return voidMenuStart() + kMenu; }
float span(uint32_t t, uint32_t from, uint32_t len) { return t <= from ? 0.f : t >= from + len ? 1.f : float(t - from) / float(len); }
}  // namespace intro

// The Simplistic title's planet rising into place during the pan (the cached background has it fixed): the space background
// without it, darkened while the sky fades in, and its circles drawn lower.
void simplisticIntro(Canvas& c, float pan, float sky) {
    drawSpaceBackground(c, false);
    {
        const int ox = c.viewX(), vw = c.width();
        const Rect clip = c.clip();
        c.setView(0, c.fullWidth());
        if (sky < 1.f) c.fillRect(0, 0, c.width(), c.height(), rgb(0, 0, 0), int((1.f - sky) * 255.f));
        const float e = pan * pan * (3.f - 2.f * pan);
        const float dx = float(c.width() - theme::kScreenW), lift = (1.f - e) * 440.f;
        if (lift < 300.f + 20.f) {
            c.fillCircle(560 + dx, 600 + lift, 300, rgb(14, 22, 44));
            c.fillCircle(585 + dx, 625 + lift, 290, rgb(10, 15, 30));
            c.ring(560 + dx, 600 + lift, 300, 2.5f, rgb(70, 120, 200), 150);
        }
        c.setView(ox, vw);
        c.setClip(clip);
    }
}

class TitleScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t now) override {
        if (!creditsTest_) {  // screen tests: open the Credits straight away
            creditsTest_ = true;
            if (std::getenv("TI4_SHOW_CREDITS")) credits_ = true;
        }
        // 0.30.0 (owner): the title has its own track (back from a game, the game's music hands over)
        if (audio::musicTrack() != audio::kTrackTitle && audio::musicTrack() != -1) audio::musicTo(audio::kTrackTitle, 1200, 2000);
        if (!tilesetRead_) {  // the tileset chosen last time (0.26.0)
            tilesetRead_ = true;
            // 0.32.0: 2 = Voidfall; 0.57.2 (owner): Retrowave is the default
            retro::setTileset(app.settings().getInt("tileset", retro::kRetrowave));
            gunits::setFactionUnits(app.settings().getInt("ground_units", 1) != 0);  // 0.44.0
            items_.clear();
        }
        decideIntro(app, now);
        if (introState_ == 1) {
            introTick(now);
            bool skip = in.mouse().click || in.mouse().rightClick;
            for (int b = 0; b <= int(Btn::Menu); ++b) skip = skip || in.pressed(Btn(b));
            if (skip) {  // any button: straight to the menu (and the music - from its beginning, as after the whole opening)
                introState_ = 0;
                audio::releaseMusic(1500, true);
            }
            return;
        }
        buildItems();
        const int n = int(items_.size());
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (sel_ >= n) sel_ = 0;
        // Credits (0.28.0, owner): the black hole in the top-left corner, for the mouse only
        const Mouse& m = in.mouse();
        const float mx = m.x - float(theme::legacyX());  // (widescreen: the title screen is drawn centred)
        creditsHover_ = m.active && mx >= 8.f && mx < 104.f && m.y >= 4.f && m.y < 104.f;
        if (credits_) {
            if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Menu) || in.pressed(Btn::Start)) credits_ = false;
            return;
        }
        if (in.pressed(Btn::Menu)) app.quit();
        // Tilesets (0.26.0, owner): Simplistic (the drawn shapes), Retrowave (animated pixel art) and
        // Voidfall (0.32.0: high definition, animated); left / right go round them, missing art is skipped.
        // 0.57.2 (owner): in the order Retrowave, Voidfall, Simplistic
        if (ids_[sel_] == "tileset" && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
            error_.clear();
            static const int kOrder[retro::kTilesets] = {retro::kRetrowave, retro::kVoidfall, retro::kSimplistic};
            const int dir = in.pressed(Btn::Left) ? -1 : 1;
            int pos = 0;
            for (int k = 0; k < retro::kTilesets; ++k)
                if (kOrder[k] == retro::tileset()) pos = k;
            int t = retro::tileset();
            for (int k = 0; k < retro::kTilesets; ++k) {
                pos = (pos + dir + retro::kTilesets) % retro::kTilesets;
                t = kOrder[pos];
                if (retro::tilesetAvailable(t)) break;
                error_ = t == retro::kRetrowave ? "The Retrowave art (retro.dat) is missing - copy it next to the game."
                                                : "The Voidfall art (voidfall-*.dat) is missing - copy it next to the game.";
            }
            if (retro::tilesetAvailable(t)) error_.clear();
            retro::setTileset(t);
            app.settings().setInt("tileset", retro::tileset());
            app.settings().save();
            items_[size_t(sel_)].label = tilesetLabel();
            return;
        }
        if (in.pressed(Btn::A) || in.pressed(Btn::Start)) {
            const std::string& id = ids_[sel_];
            error_.clear();
            if (id == "continue") {
                std::unique_ptr<GameSession> s(new GameSession());
                std::string err;
                if (s->loadSaved(&err)) app.push(makeGameScene(std::move(s)));
                else error_ = "Couldn't load the saved game: " + err;
            } else if (id == "new") {
                app.push(makeNewGameScene());
            } else if (id == "tournament") {
                app.push(makeTournamentScene());
            } else if (id == "campaigns") {
                app.push(makeCampaignScene());
            } else if (id == "modules") {
                app.push(makeModulesScene());
            } else if (id == "options") {
                app.push(makeOptionsScene());
            } else if (id == "tests") {
                app.push(makeTestsMenuScene());
            } else if (id == "quit") {
                app.quit();
            }
        }
    }

    bool animating() const override { return true; }

    void draw(App& app, Canvas& c, uint32_t now) override {
        buildItems();
        decideIntro(app, now);
        // 0.59.70: during the opening the sky, the pan and the menu follow its clock (t: ms into it; -1 waiting for the sounds)
        const bool opening = introState_ == 1;
        const int t = opening ? introClock(now) : 0;
        const bool vt = voidTitle();
        const uint32_t panStart = vt ? intro::voidPanStart() : intro::kPanStart, panLen = vt ? intro::kVoidPan : intro::kPan;
        const uint32_t menuStart = vt ? intro::voidMenuStart() : intro::kMenuStart;
        const float pan = !opening ? 1.f : t < 0 ? 0.f : intro::span(uint32_t(t), panStart, panLen);
        const float sky = !opening ? 1.f : t < 0 ? 0.f : intro::span(uint32_t(t), intro::kSkyStart, intro::kSky);
        const int fade = !opening ? 255 : t < 0 ? 0 : int(255.f * intro::span(uint32_t(t), menuStart, intro::kMenu));
        if (retroTitle()) titlebg::draw(c, now, pan, sky);  // (with its own stars)
        else if (vt) titlewar::draw(c, now, pan, sky);
        else {
            if (opening) simplisticIntro(c, pan, sky);
            else drawSpaceBackground(c, true);
            drawMarquee(c, now);
        }
        framed_ = true;
        if (fade > 0) drawBlackHole(c, now, fade);
        drawName(c, opening ? t : -2);
        drawTextAligned(c, FONT_MD, 0, 88, c.width(), Align::Center, "Fourth Edition \xC2\xB7 Prophecy of Kings", theme::text, fade);
        drawTextAligned(c, FONT_SM, 0, 110, c.width(), Align::Center,
#ifdef _WIN32
                        "Windows version \xE2\x80\x94 " TI4_STAGE, theme::dim, fade);
#elif defined(TI4_SDL2)
                        "Steam Deck / Linux version \xE2\x80\x94 " TI4_STAGE, theme::dim, fade);
#else
                        "Miyoo Mini Plus homebrew \xE2\x80\x94 " TI4_STAGE, theme::dim, fade);
#endif
        int n = int(items_.size());
        // 0.80.0 (owner 2026-11-19: "a row of the earned medals on the title screen, under the menu") - room for it
        const std::vector<std::pair<std::string, uint32_t>> medals = profile::earnedEmblems();
        const int rowH = !medals.empty() && n > 6 ? 30 : n > 7 ? 33 : n > 5 ? 36 : 40;
        Rect menu{150, n > 5 ? 148 : 160, 340, n * rowH};
        if (fade > 0) {
            titlePanel(c, Rect{menu.x - 14, menu.y - 12, menu.w + 28, menu.h + 18}, 12, fade);
            titleMenu(c, menu, items_, sel_, now, &sel_, rowH, fade);
        }
        if (fade >= 255 && !medals.empty()) leagueui::drawEmblemRow(c, menu.y + menu.h + 22, medals, now);
        if (opening && t > 1500 && fade == 0)  // a quiet line: the opening can be skipped
            drawTextAligned(c, FONT_XS, 0, 456, c.width(), Align::Center, "Press any button to skip", rgb(150, 140, 180),
                            int(90.f * intro::span(uint32_t(t), 1500, 800)));
        if (fade < 255) return;
        if (sel_ < int(ids_.size()) && ids_[size_t(sel_)] == "tileset") {
            // a few tiles either side, in the chosen tileset
            static const char* left[] = {"18", "42", "36"}, *right[] = {"39", "43", "26"};
            auto defOf = [](const char* key) {
                for (int i = 0; i < ti::kSystemCount; ++i)
                    if (!std::strcmp(ti::kSystems[i].key, key)) return i;
                return -1;
            };
            for (int k = 0; k < 3; ++k) {
                MapView::drawTile(c, defOf(left[k]), 68.f, 196.f + 62.f * float(k), 34.f, now);
                MapView::drawTile(c, defOf(right[k]), 572.f, 196.f + 62.f * float(k), 34.f, now);
            }
        }
        if (!error_.empty()) {
            int ey = 160 + n * 40 + 4;
            for (const std::string& l : wrapText(FONT_SM, error_, c.width() - 60)) {
                drawTextAligned(c, FONT_SM, 0, ey, c.width(), Align::Center, l, theme::red);
                ey += 16;
            }
        }
        // 0.85.1: the first start of this version brought the profile in from an older copy - said for the first minute, in place
        // of the version lines
        static uint32_t importShownFrom = 0;
        if (!importShownFrom) importShownFrom = now ? now : 1;
        const bool importNote = bugreport::crashFolder().empty() && paths::importedCount() > 0 && now - importShownFrom < 60000;
        if (importNote) {
            std::unique_ptr<TextHalo> halo(artTitle() ? new TextHalo(170) : nullptr);
            const std::string msg = "Your profile, settings and saved game were brought over from " + paths::importedFrom() +
                                    ". They now live in " + paths::shownDir() + " - every new version finds them there.";
            int ey = 446 - 2 * 13;
            for (const std::string& l : wrapText(FONT_XS, msg, c.width() - 80)) {
                drawTextAligned(c, FONT_XS, 0, ey, c.width(), Align::Center, l, theme::gold);
                ey += 13;
            }
        } else {
            std::unique_ptr<TextHalo> halo(artTitle() ? new TextHalo(170) : nullptr);  // (0.59.70: over the planet's clouds)
            drawTextAligned(c, FONT_XS, 0, 423, c.width(), Align::Center,
                            "Version " TI4_VERSION " \xC2\xB7 Unofficial Fan Project of Twilight Imperium", artTitle() ? rgb(150, 140, 180) : theme::faint);
            // 0.81.0 (owner, from Asmodee on the phone 2026-11-21): say the game isn't affiliated with them
            drawTextAligned(c, FONT_XS, 0, 436, c.width(), Align::Center,
                            "Not affiliated with or endorsed by Asmodee or Fantasy Flight Games", artTitle() ? rgb(120, 112, 150) : theme::faint);
        }
        if (!bugreport::crashFolder().empty()) {  // 0.43.3: the last run crashed - a report was made by itself
            const std::string msg = "The game closed unexpectedly last time. A crash report was saved in the folder " +
                                    bugreport::crashFolder() + " (next to the game) - please send it.";
            int ey = 446 - 2 * 13;
            for (const std::string& l : wrapText(FONT_XS, msg, c.width() - 80)) {
                drawTextAligned(c, FONT_XS, 0, ey, c.width(), Align::Center, l, theme::red);
                ey += 13;
            }
        }
        // the track playing, like a watermark (0.28.0, owner)
        if (audio::musicPlaying() && audio::musicTrack() >= 0)
            drawTextAligned(c, FONT_XS, 0, 6, c.width() - 10, Align::Right,
                            std::string("\xE2\x99\xAA ") + audio::trackTitle(audio::musicTrack()) + " / " +
                                audio::trackAlbum(audio::musicTrack()) + " \xC2\xB7 Michael Keane",
                            rgb(70, 78, 104));  // top right (owner, 0.28.3)
        drawHintBar(c, {{"A", "Open"}, {"\xE2\x86\x91\xE2\x86\x93", "Choose"}, {"MENU", "Quit"}});
        if (credits_) drawCredits(c, now);
    }

private:
    // ---- 0.59.70: the opening
    // once per run of the game (not in the automated tests unless TI4_TITLEINTRO=1; TI4_NOINTRO=1 leaves it out)
    void decideIntro(App& app, uint32_t now) {
        if (introState_ >= 0) return;
        if (!tilesetRead_) {  // (the tileset decides the backdrop)
            tilesetRead_ = true;
            retro::setTileset(app.settings().getInt("tileset", retro::kRetrowave));
            gunits::setFactionUnits(app.settings().getInt("ground_units", 1) != 0);
            items_.clear();
        }
        static bool seen = false;
        const bool want = !seen && !std::getenv("TI4_NOINTRO") && (!app.platform().headless() || std::getenv("TI4_TITLEINTRO"));
        seen = true;
        introState_ = want ? 1 : 0;
        introBegin_ = now;
        if (retroTitle()) titlebg::prepare();  // the planet is made in the background meanwhile
        if (voidTitle()) titlewar::prepare();  // the battle starts (a little ahead)
        if (retroTitle() && app.platform().headless()) titlebg::finish();  // (the automated tests: the same picture every run)
        if (!want) audio::releaseMusic(1500);   // (nothing to wait for)
    }
    // ms into the opening; -1 while it waits for the sound effects to be made (at most intro::kWait)
    int introClock(uint32_t now) {
        if (!introT0_) {
            // (the clock starts once a first frame is up: on the Miyoo the Voidfall battlefield takes a moment to paint)
            if (!framed_ || (!audio::soundsReady() && now - introBegin_ < intro::kWait)) return -1;
            introT0_ = now ? now : 1u;
        }
        return int(now - introT0_);
    }
    // its sounds, each once when its moment comes; then the music; then the end
    void introTick(uint32_t now) {
        using namespace intro;
        const int t = introClock(now);
        if (t < 0) return;
        const uint32_t ut = uint32_t(t);
        auto panOf = [](int i) { return -0.7f + 1.4f * float(i) / float(kLetters - 1); };  // left to right across the speakers
        while (glyphs_ < kLetters && ut >= kFirst + uint32_t(glyphs_) * kStep) {
            if (kName[glyphs_] != ' ') audio::play(audio::Sfx::TitleGlyph, 0.85f, panOf(glyphs_));
            ++glyphs_;
        }
        // (owner 12:13: "have no sound for the font change" - the phase shift is silent; Sfx::TitleDecode is no longer played)
        if (!revealed_ && ut >= revealAt()) {
            audio::play(audio::Sfx::TitleReveal, 1.f);
            revealed_ = true;
        }
        if (ut >= musicAt()) audio::releaseMusic(int(kMusicFade), true);  // (only the first call starts it; from the song's start)
        if (ut >= (voidTitle() ? voidEndAt() : endAt())) introState_ = 0;
    }
    // the middle of a letter's drawing, down from the top of its line
    static int glyphMid(const FontData& f, uint32_t cp) {
        for (int i = 0; i < f.count; ++i)
            if (f.glyphs[i].cp == cp) return f.ascent + f.glyphs[i].yo + f.glyphs[i].h / 2;
        return f.ascent / 2;
    }
    // The name: Lazaxian letters coming one by one from the left, each phasing into its English letter a moment later; when the last has turned, the name flares and its underline grows (t = ms into the opening, -2 = done).
    void drawName(Canvas& c, int t) const {
        using namespace intro;
        const std::string name = kName;
        const int y = 34, x0 = (c.width() - textWidth(FONT_XL, name)) / 2;
        const bool neon = artTitle();
        const uint32_t glowC = voidTitle() ? rgb(255, 110, 50) : rgb(255, 60, 200);
        auto english = [&](int x, const std::string& s, uint32_t col, int a) {
            if (neon)  // a neon glow round the gold (magenta; embers in Voidfall)
                for (int k = 0; k < 4; ++k) drawText(c, FONT_XL, x + (k == 0 ? -1 : k == 1 ? 1 : 0), y + (k == 2 ? -1 : k == 3 ? 1 : 0), s, glowC, 55 * a / 255);
            drawText(c, FONT_XL, x, y, s, col, a);
        };
        // the underline and the emblems either side (they come with the flare)
        const float deco = t == -2 ? 1.f : t < 0 ? 0.f : span(uint32_t(t), revealAt(), 700);
        if (deco > 0.f) {
            const int half = int(150.f * deco);
            c.fillRect(320 - half, 80, 2 * half, 1, neon ? (voidTitle() ? rgb(255, 140, 70) : rgb(255, 90, 210)) : theme::goldDim);
            icons::draw(c, "pok", 140, 70, 22, int(255.f * deco), theme::gold);
            icons::draw(c, "pok", 478, 70, 22, int(255.f * deco), theme::gold);
        }
        if (t == -2) {
            english(x0, name, theme::gold, 255);
            return;
        }
        if (t < 0) return;
        const uint32_t ut = uint32_t(t);
        const float flare = ut >= revealAt() ? 1.f - span(ut, revealAt(), 900) : 0.f;
        const uint32_t gold = mix(theme::gold, rgb(255, 244, 214), int(flare * 255.f));
        const int top = y + 2, bot = y + FONT_XL.lineHeight;
        const int capMid = glyphMid(FONT_XL, 'E');
        const Rect clip = c.clip();
        for (int i = 0; i < kLetters; ++i) {
            if (name[size_t(i)] == ' ') continue;
            const uint32_t at = kFirst + uint32_t(i) * kStep;
            if (ut < at) break;
            const std::string ch = name.substr(size_t(i), 1);
            const int lx = x0 + textWidth(FONT_XL, name.substr(0, size_t(i))), lw = textWidth(FONT_XL, ch);
            // (centred on the English capital: the Lazaxian letters are shorter and sit lower on their line)
            const int zw = textWidth(FONT_LAZXL, ch), zx = lx + lw / 2 - zw / 2, zy = y + capMid - glyphMid(FONT_LAZXL, uint32_t(name[size_t(i)]));
            const float come = span(ut, at, kFade), m = span(ut, at + kMorphLag, kMorph);  // m: 0 Lazaxian .. 1 English
            // The phase shift (owner 12:12: "a phase transformation of the letters from Lazaxian to English"): the Lazaxian
            // letter goes out of phase - its rows slide apart in a rippling wave, its colours split and it fades - while the
            // English letter comes into phase the other way, from a faint ripple into the still gold letter.
            const float out = std::min(1.f, m / 0.65f), in = std::max(0.f, (m - 0.35f) / 0.65f);
            const float wave = float(ut % 1000000u) * 0.016f + float(i) * 1.7f;
            auto phased = [&](bool lazaxian, int gx, int gy, uint32_t col, int a, float amp) {
                if (a <= 0) return;
                const FontData& f = lazaxian ? FONT_LAZXL : FONT_XL;
                if (amp < 0.4f) {  // in phase: the whole letter, with its glow
                    if (lazaxian) {
                        for (int k = 0; k < 4; ++k)
                            drawText(c, f, gx + (k == 0 ? -1 : k == 1 ? 1 : 0), gy + (k == 2 ? -1 : k == 3 ? 1 : 0), ch, rgb(60, 190, 255), 70 * a / 255);
                        drawText(c, f, gx, gy, ch, col, a);
                    } else {
                        english(gx, ch, col, a);
                    }
                    return;
                }
                const int split = std::max(1, int(amp * 0.45f + 0.5f)), w = (lazaxian ? zw : lw) + 40;
                for (int by = top; by < bot; by += 2) {  // two units a slice
                    const int ox = int(std::lround(amp * std::sin(wave + float(by - top) * 0.5f)));
                    c.setClip(Rect{gx - 20, by, w, 2});
                    drawText(c, f, gx + ox - split, gy, ch, rgb(255, 60, 200), a * 2 / 5);  // the colours drifting apart
                    drawText(c, f, gx + ox + split, gy, ch, rgb(60, 220, 255), a * 2 / 5);
                    drawText(c, f, gx + ox, gy, ch, col, a);
                }
                c.setClip(clip);
            };
            if (out < 1.f) {  // the Lazaxian letter: pale cyan with a glow, a white flash as it comes; then out of phase
                const int a = int(255.f * come * (1.f - out) * (1.f - out * 0.4f));
                phased(true, zx, zy, mix(rgb(176, 242, 255), rgb(255, 255, 255), int(out * 140.f)), a, 7.f * out * std::sqrt(out));
                if (ut - at < 240) drawText(c, FONT_LAZXL, zx, zy, ch, rgb(255, 255, 255), int(220.f * (1.f - float(ut - at) / 240.f)));
            }
            if (in > 0.f) {  // the English letter coming into phase, cyan warming to gold
                const float still = 1.f - in;
                phased(false, lx, y, mix(rgb(200, 245, 255), gold, int(255.f * in * in)), int(255.f * std::min(1.f, in * 1.3f)), 7.f * still * still);
            }
        }
    }

    void buildItems() {
        // check for a saved game now and then, not every frame
        if (!items_.empty() && ++checkTimer_ % 90 != 0) return;
        bool save = GameSession::saveExists();
        if (save == hadSave_ && !items_.empty()) return;
        hadSave_ = save;
        items_.clear();
        ids_.clear();
        if (save) {
            items_.push_back({"Continue game", "Pick up where you left off"});
            ids_.push_back("continue");
        }
        // 0.80.0 (the owner's League Designer page: "Campaign", "Tournament", "Casual Game"); Device tests moved into Options
        items_.push_back({"Campaigns", "Eve of the Fall, Fated Expedition, Trade Wars \xC2\xB7 coming soon"});
        ids_.push_back("campaigns");
        items_.push_back({"Career", "Leagues against rival commanders who remember you"});
        ids_.push_back("tournament");
        items_.push_back({"Casual game", "3 to 8 players against the computer"});
        ids_.push_back("new");
        items_.push_back({"Modules", "Expansions and add-ons in your next game"});
        ids_.push_back("modules");
        items_.push_back({tilesetLabel(), "\xE2\x97\x80 \xE2\x96\xB6 Simplistic, Retrowave (pixel art) or Voidfall (painted)"});
        ids_.push_back("tileset");
#if defined(_WIN32) || defined(TI4_SDL2)
        items_.push_back({"Options", "Music, sounds, controls and screen"});
#else
        items_.push_back({"Options", "Music and sound volume"});
#endif
        ids_.push_back("options");
#if defined(_WIN32) || defined(TI4_SDL2)
        items_.push_back({"Quit", "Close the game"});
#else
        items_.push_back({"Quit", "Back to OnionOS"});
#endif
        ids_.push_back("quit");
        // (0.80.0) the cursor starts on Continue game when there is one, else on Casual game (Campaigns, first in the list,
        // is still "coming soon")
        sel_ = int(std::find(ids_.begin(), ids_.end(), save ? "continue" : "new") - ids_.begin());
    }

    static std::string tilesetLabel() { return std::string("Tilesets: ") + retro::tilesetName(retro::tileset()); }

    // A rolling marquee of stars (0.28.0, owner): three layers drifting left at their own speeds.
    // 0.58.4 (owner: "cut off where the aspect ratio for the miyoo mini was"): on a widescreen PC the title is laid
    // out in the middle 640 units, so the stars are drawn on the whole canvas (view reset, as many per unit of width).
    static void drawMarquee(Canvas& c, uint32_t now) {
        static const int kSpeed[3] = {6, 13, 26};  // units per second
        const int ox = c.viewX(), vw = c.width();
        const Rect clip = c.clip();
        c.setView(0, c.fullWidth());
        const int W = c.width();
        const int n = 46 * W / 640;
        for (int layer = 0; layer < 3; ++layer)
            for (int i = 0; i < n; ++i) {
                uint32_t h = uint32_t(i * 2654435761u) ^ uint32_t(layer * 40503u + 977u);
                h ^= h >> 15;
                h *= 2246822519u;
                h ^= h >> 13;
                const int y = int(h % 452u);
                const int x0 = int((h >> 9) % uint32_t(W));
                const int x = ((x0 - int((uint64_t(now) * uint64_t(kSpeed[layer]) / 1000u) % uint64_t(W))) % W + W) % W;
                const int a = 70 + layer * 60 + int((h >> 20) % 40u);
                const uint32_t col = (h >> 3) % 7 == 0 ? rgb(170, 200, 255) : (h >> 3) % 11 == 0 ? rgb(255, 210, 170) : rgb(230, 234, 245);
                if (layer == 2) c.fillRect(x, y, 2, 2, col, a);
                else c.fillRect(x, y, 1, 1, col, a);
            }
        c.setView(ox, vw);
        c.setClip(clip);
    }
    // The Credits black hole (0.28.0): a dark heart, a turning ring of neon dust, a glow when the mouse is on it.
    // (0.59.70: fade < 255 while the opening's menu fades in)
    void drawBlackHole(Canvas& c, uint32_t now, int fade = 255) const {
        const float cx = 56.f, cy = 50.f;
        const float t = float(now % 12000u) / 12000.f * 6.2831853f;
        auto f = [fade](int a) { return a * fade / 255; };
        if (creditsHover_) c.fillCircle(cx, cy, 46.f, rgb(255, 60, 200), f(40));
        for (int i = 0; i < 90; ++i) {  // the accretion disc, flattened, two colours
            const float a = t * (i % 3 == 0 ? 1.4f : 1.f) + float(i) * 0.0698f * 1.0f;
            const float rr = 22.f + float((i * 37) % 17);
            const float x = cx + std::cos(a) * rr, y = cy + std::sin(a) * rr * 0.42f;
            const bool front = std::sin(a) > 0.f;
            const uint32_t col = i % 2 ? rgb(255, 70, 210) : rgb(70, 220, 255);
            const int al = creditsHover_ ? 255 : (front ? 210 : 120);
            c.fillRect(int(x), int(y), 2, 2, col, f(al));
        }
        c.fillCircle(cx, cy, 15.f, rgb(255, 120, 230), f(creditsHover_ ? 150 : 90));  // the photon ring
        c.fillCircle(cx, cy, 13.f, rgb(0, 0, 0), f(255));                              // the heart
        for (int i = 0; i < 24; ++i) {  // the near side of the disc passes in front of it
            const float a = t + float(i) * 0.13f;
            if (std::sin(a) <= 0.f) continue;
            const float rr = 22.f + float((i * 37) % 17);
            c.fillRect(int(cx + std::cos(a) * rr), int(cy + std::sin(a) * rr * 0.42f), 2, 2, i % 2 ? rgb(255, 70, 210) : rgb(70, 220, 255), f(230));
        }
        drawTextAligned(c, FONT_SMB, int(cx) - 40, int(cy) + 30, 80, Align::Center, "Credits",
                        creditsHover_ ? rgb(255, 140, 230) : rgb(120, 110, 150), fade);
        if (fade >= 255) hits::addAction(Rect{8, 4, 96, 100}, [this]() { credits_ = true; }, hits::Light::None);  // (its own glow)
    }
    static void drawCredits(Canvas& c, uint32_t now) {
        backdrops::drawCredits(c, now);  // 0.30.0 (owner): darker, a starscape rolling left, ships in hyperspace
        drawTextAligned(c, FONT_XL, 0, 70, 640, Align::Center, "Credits", theme::gold);
        c.fillRect(220, 112, 200, 1, theme::goldDim);
        static const char* who[][2] = {{"Christian T. Peterson", "Creator of Twilight Imperium"},
                                       {"Asmodee & Fantasy Flight Games", "IP and copyright holders of the official TI4 content"},
                                       {"Mobiusuchronic", "Playtester"},
                                       {"Warchem", "Designer"},
                                       {"Michael Keane", "Music: The Council Keleres, Infiltration (Embers of the Imperium), Action (Twilight Symposium)"},
                                       {"Absol197", "Absol's Agendas (the optional agenda deck)"},  // 0.81.0
                                       // 0.82.0 (owner 2026-11-21, Q7: credits Cacotopos + Blarknob); 0.82.4: the roles as the
                                       // Monuments+ rulebook gives them ("Design: Tom Dullemond (Cacotopos)", "Blarknob for the original
                                       // Monuments expansion")
                                       {"Cacotopos & Blarknob", "Monuments+ (the optional module): by Cacotopos, after Blarknob's original Monuments"},
                                       // 0.83.0: Blue Reverie (its faction portraits are Cacotopos's, made into our pixel art)
                                       {"TacticBlue", "Blue Reverie (the optional module): its factions - portraits by Cacotopos"}};
        const int n = int(sizeof who / sizeof who[0]), step = n > 7 ? 36 : n > 6 ? 40 : 46;  // (0.83.0: eight names)
        int y = 124;
        for (const auto& w : who) {
            drawTextAligned(c, FONT_LG, 0, y, 640, Align::Center, w[0], theme::text);
            drawTextAligned(c, FONT_SM, 0, y + 22, 640, Align::Center, w[1], theme::dim);
            y += step;
        }
        // 0.81.0 (owner, from Asmodee 2026-11-21)
        drawTextAligned(c, FONT_XS, 0, y + 6, 640, Align::Center,
                        "Fallen Imperium is an unofficial fan project. It is not affiliated with, endorsed or sponsored by", theme::dim);
        drawTextAligned(c, FONT_XS, 0, y + 19, 640, Align::Center,
                        "Asmodee or Fantasy Flight Games. Twilight Imperium is a trademark of Fantasy Flight Games.", theme::dim);
        drawTextAligned(c, FONT_XS, 0, 446, 640, Align::Center, "Click or press any button to close", theme::faint);
    }

    int sel_ = 0, checkTimer_ = 0;
    bool hadSave_ = false, tilesetRead_ = false;
    int introState_ = -1;                    // 0.59.70: -1 not decided yet, 1 the opening runs, 0 the menu
    uint32_t introBegin_ = 0, introT0_ = 0;  // when the title came up; when the opening's clock started
    int glyphs_ = 0;                         // the letters whose chimes have played
    bool revealed_ = false;
    bool framed_ = false;  // a first frame has been drawn
    bool creditsHover_ = false, creditsTest_ = false;
    mutable bool credits_ = false;
    std::vector<MenuItem> items_;
    std::vector<std::string> ids_;
    std::string error_;
};

// Options (0.27.0): music and sound volume; on Windows / the Steam Deck also the
// controls (keyboard & mouse or a controller), the picture (sharp at the
// monitor's resolution or the original 640x480) and full screen.
class OptionsScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t) override {
        build(app);
        const int n = int(ids_.size());
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (sel_ >= n) sel_ = n - 1;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        const int dir = in.pressed(Btn::Left) ? -1 : in.pressed(Btn::Right) ? 1 : in.pressed(Btn::A) ? 2 : 0;
        if (!dir) return;
        Settings& st = app.settings();
        const std::string& id = ids_[size_t(sel_)];
        if (id == "back") {
            if (dir == 2) app.pop();
            return;
        }
        if (id == "player") {  // 0.68.0 (owner): the music player
            if (dir == 2) app.push(makeMusicScene());
            return;
        }
        if (id == "layout") {  // 0.28.0 (owner): both control layouts on one screen; 0.72.0 the key bindings
            if (dir == 2) app.push(makeControlsScene());
            return;
        }
        if (id == "tests") {
            if (dir == 2) app.push(makeTestsMenuScene());
            return;
        }
        if (id == "profilefolder") {  // 0.85.1: open it (Windows)
            if (dir == 2) paths::openFolder();
            return;
        }
        if (id == "report") {  // 0.28.0 (owner): the log, save and settings copied into one folder to send
            if (dir == 2) report_ = makeBugReport();
            return;
        }
        if (id == "music" || id == "sound") {
            int v = id == "music" ? audio::musicVolume() : audio::soundVolume();
            v = dir == -1 ? std::max(0, v - 1) : dir == 1 ? std::min(10, v + 1) : (v + 1) % 11;
            if (id == "music") audio::setMusicVolume(v);
            else audio::setSoundVolume(v);
            st.setInt(id == "music" ? "music_vol" : "sound_vol", v);
            if (dir != 2) audio::play(id == "sound" ? audio::Sfx::Railgun : audio::Sfx::Toggle);
        } else if (id == "mapsounds") {  // 0.69.0: the siren, the landings, the factions on the move
            st.setInt("map_sounds", st.getInt("map_sounds", 1) != 0 ? 0 : 1);
        } else if (id == "ground") {  // 0.44.0 (owner): the faction ground units, or the Default for everyone
            const bool on = st.getInt("ground_units", 1) == 0;
            st.setInt("ground_units", on ? 1 : 0);
            gunits::setFactionUnits(on);
        } else if (id == "habits") {  // 0.54.0: the bots learn the player's habits
            habits::setEnabled(st, !habits::enabled(st));
        } else if (id == "controls") {
            st.setInt("controls", st.getInt("controls", 0) == 0 ? 1 : 0);
        } else if (id == "screen") {  // (0.46.0) Sharp widescreen -> Sharp 4:3 -> Original 640x480 -> ...
            const bool sharp = st.getInt("screen", 1) == 1, wide = st.getInt("wide", 1) == 1;
            const int at = !sharp ? 2 : wide ? 0 : 1, next = (at + (dir == -1 ? 2 : 1)) % 3;
            st.setInt("screen", next == 2 ? 0 : 1);
            st.setInt("wide", next == 0 ? 1 : 0);
        } else if (id == "fullscreen") {
            st.setInt("fullscreen", st.getInt("fullscreen", 1) == 1 ? 0 : 1);
        } else if (id == "unlock") {  // 0.80.0: the testing switch - every league, colour and emblem source open
            profile::setUnlockAll(!profile::unlockAll());
            profile::save();
        } else if (id == "edge") {  // 0.72.0 (owner: "Keep the edge scrolling off but available in the main menu options")
            st.setInt("edge_scroll", st.getInt("edge_scroll", 0) != 0 ? 0 : 1);
        }
        st.save();
        app.platform().configure(st);
        if (id != "music" && id != "sound" && dir != 2) audio::play(audio::Sfx::Toggle);
    }
    bool animating() const override { return true; }
    void draw(App& app, Canvas& c, uint32_t now) override {
        build(app);
        titleBackdrop(c, now);
        const int n = int(items_.size());
        drawTextAligned(c, FONT_LG, 0, n > 15 ? 8 : n > 13 ? 14 : n > 11 ? 24 : 40, c.width(), Align::Center, "Options", theme::gold);
        const int rowH = n > 15 ? 24 : n > 14 ? 26 : n > 13 ? 27 : n > 12 ? 29 : n > 11 ? 30 : n > 10 ? 32 : n > 9 ? 34 : n > 8 ? 36 : 40;  // (0.44.0: nine rows on the PC; 0.54.0: ten; 0.68.0: eleven; 0.69.0: twelve; 0.72.0: thirteen; 0.80.0: fifteen)
        Rect menu{130, n > 15 ? 44 : n > 13 ? 52 : n > 12 ? 64 : n > 11 ? 72 : n > 10 ? 74 : n > 9 ? 78 : n > 8 ? 84 : 96, 380, n * rowH};
        titlePanel(c, Rect{menu.x - 14, menu.y - 12, menu.w + 28, menu.h + 18}, 12);
        titleMenu(c, menu, items_, sel_, now, &sel_, rowH);
        drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", "Change"}, {"B", "Back"}});
    }

private:
    static std::string bar(int v) {
        std::string b;
        for (int i = 0; i < 10; ++i) b += i < v ? "\xE2\x80\xA2" : "\xC2\xB7";
        return b;
    }
    void build(App& app) {
        const Settings& st = app.settings();
        items_.clear();
        ids_.clear();
        auto add = [&](const char* id, const std::string& label, const std::string& detail) {
            ids_.push_back(id);
            items_.push_back({label, detail});
        };
        const int mv = audio::musicVolume(), sv = audio::soundVolume();
        add("music", "Music: " + (mv ? std::to_string(mv) : std::string("Off")) + "   " + bar(mv),
            audio::running() ? "\xE2\x97\x80 \xE2\x96\xB6 the music's volume (0 = off)" : "No sound on this device (see System info)");
        add("sound", "Sounds: " + (sv ? std::to_string(sv) : std::string("Off")) + "   " + bar(sv),
            "\xE2\x97\x80 \xE2\x96\xB6 buttons and battle sounds (0 = off)");
        {  // 0.69.0 (owner: the grim map sounds)
            const bool on = st.getInt("map_sounds", 1) != 0;
            add("mapsounds", std::string("Map sounds: ") + (on ? "On" : "Off"),
                on ? "Sirens, invasions and each faction's ships on the move" : "No sirens, invasions or ships on the move");
        }
        {  // 0.68.0 (owner): your own music in games
            const int m = audio::musicMode();
            add("player", "Music player", m == audio::kPlayMine   ? "Games play your own music (My Music)"
                                          : m == audio::kPlayBoth ? "Games play the game soundtrack and your own music"
                                                                  : "Games play the game soundtrack - add your own MP3s here");
        }
        const bool fu = st.getInt("ground_units", 1) != 0;
        add("ground", std::string("Ground combat: ") + (fu ? "Faction units" : "Default animations"),
            fu ? "Each faction fights with its own soldiers, mechs and effects" : "The game's own soldiers and mechs for every faction");
        {  // 0.54.0 (owner): the bots adapt to how you play
            const bool on = habits::enabled(st);
            const int games = habits::load(st).games;
            add("habits", std::string("Bots learn your habits: ") + (on ? "On" : "Off"),
                !on        ? "Bots know nothing of how you played before"
                : games > 0 ? "They remember " + std::to_string(games) + (games == 1 ? " game" : " games") + ": your attacks, raids, Mecatol Rex, promises"
                            : "From your first game on: your attacks, raids, Mecatol Rex, promises");
        }
#if defined(_WIN32) || defined(TI4_SDL2)
        const bool pad = st.getInt("controls", 0) == 1;
        add("controls", std::string("Controls: ") + (pad ? "Controller" : "Keyboard & mouse"),
            pad ? "A gamepad (the camera keys move the cursor); the mouse still works"
                : "The keys move the camera (" + keyLabel("D-PAD").substr(keyLabel("D-PAD").find('/') + 1) + "), the mouse points, clicks and drags");
        const bool sharp = st.getInt("screen", 1) == 1, wide = sharp && st.getInt("wide", 1) == 1;
        add("screen", std::string("Picture: ") + (wide ? "Sharp widescreen" : sharp ? "Sharp 4:3" : "Original 640x480"),
            wide    ? "Your screen's resolution and shape - the map uses the whole width"
            : sharp ? "Your screen's resolution, 4:3 with bars at the sides"
                    : "The Miyoo's 640x480 picture, enlarged");
        add("fullscreen", std::string("Full screen: ") + (st.getInt("fullscreen", 1) == 1 ? "On" : "Off"), "Also F11 or Alt+Enter");
        {  // 0.72.0 (owner): edge scrolling, off unless asked for
            const bool on = st.getInt("edge_scroll", 0) != 0;
            add("edge", std::string("Edge scrolling: ") + (on ? "On" : "Off"),
                on ? "The map moves when the pointer touches the edge of the screen" : "Off: the map moves with the camera keys or a right drag");
        }
        add("layout", "Controls and key bindings", "Every key, mouse button and controller button - choose your own keys");
#else
        (void)st;
#endif
        {  // 0.80.0 (owner's plan: "an unlock-everything switch for testing")
            const bool on = profile::unlockAll();
            add("unlock", std::string("Unlock everything (testing): ") + (on ? "On" : "Off"),
                on ? "Every league and faction colour is open without earning it" : "Leagues and faction colours are earned in the Career");
        }
        if (!paths::userDir().empty())  // 0.85.1 (owner): leagues, unlocks, settings and the save - kept between versions (PC only)
            add("profilefolder", "Your profile folder",
#ifdef _WIN32
                paths::shownDir() + " - A opens it");
#else
                paths::shownDir());
#endif
        add("tests", "Device tests", "Buttons, screen and speed checks");  // (0.80.0: moved here from the title menu)
        add("report", "Make a bug report", report_.empty() ? "Copies ti4.log, your save and settings into one folder to send"
                                                           : report_);
        add("back", "Back", "");
    }
    // Copies the files that help find a bug into "bug-report-<date>-<time>" next to the game (app/bug_report.cpp).
    static std::string makeBugReport() {
        const std::string folder = bugreport::make("", true);
        return !folder.empty() ? std::string("Done: folder ") + folder + " (next to the game)" : std::string("Nothing to copy yet");
    }
    int sel_ = 0;
    std::string report_;
    std::vector<MenuItem> items_;
    std::vector<std::string> ids_;
};

// Controls and key bindings (0.28.0 the layout; 0.72.0, owner: "the key bindings" in the main menu's Options): every action
// with its keyboard keys - choose one and press a key to give it that key (an action that had the key gets this one's old
// key: they swap) - and the controller's buttons beside them; the mouse below. Saved in settings.cfg (core/keymap).
class ControlsScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t now) override {
        now_ = now;
        const int n = kRows + 1;  // (the last row: back to the defaults)
        if (capture_ >= 0) {
            const int k = keymap::takeKey();
            if (k != keymap::KNone) {
                const int act = kOrder[capture_];
                if (k == keymap::KEsc && act != keymap::Menu) {
                    note("Not changed", false);
                } else if (!keymap::bindable(k)) {
                    note(keymap::longName(k) + std::string(k == keymap::KF1 + 10 ? " is full screen" : " types numbers") +
                             " - choose another key",
                         true);
                    return;  // (still waiting)
                } else {
                    const int loser = keymap::bind(act, k);
                    keymap::save(app.settings());
                    app.settings().save();
                    std::string msg = keymap::actName(act) + std::string(": ") + keymap::longName(k);
                    if (loser >= 0) msg += "   " + std::string(keymap::actName(loser)) + ": " + keymap::allKeys(loser);
                    note(msg, false);
                    audio::play(audio::Sfx::Confirm);
                }
                capture_ = -1;
                return;
            }
            if (in.pressed(Btn::B) && in.mouse().rightClick) {  // (a right click gives up)
                capture_ = -1;
                note("Not changed", false);
            }
            return;
        }
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        if (in.pressed(Btn::A)) {
            if (sel_ == kRows) {
                keymap::reset();
                keymap::save(app.settings());
                app.settings().save();
                note("Every key is back to the defaults", false);
            } else {
                capture_ = sel_;
                keymap::takeKey();  // (the key that chose this row doesn't count)
                captureSince_ = now;
            }
        }
    }
    // while waiting for a key, nothing else reads the buttons (the key may be Space or Backspace)
    bool wantsAllButtons() const override { return capture_ >= 0; }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        now_ = now;
        titleBackdrop(c, now);
        drawTextAligned(c, FONT_LG, 0, 8, c.width(), Align::Center, "Controls and key bindings", theme::gold);
        const Rect r{12, 36, 616, 412};
        titlePanel(c, r, 12);
        const bool neon = artTitle();
        const TitlePal& pal = titlePal();
        const uint32_t ink = neon ? pal.label : theme::text, gold = neon ? pal.top : theme::gold;
        const uint32_t keyCol = neon ? pal.labelSel : theme::cyan, padCol = rgb(140, 220, 150), dim = neon ? pal.detail : theme::dim;
        const int c1 = r.x + 14, c2 = r.x + 236, c3 = r.x + 470;
        drawText(c, FONT_SMB, c1, r.y + 6, "Action", gold);
        drawText(c, FONT_SMB, c2, r.y + 6, "Keyboard (choose to change)", gold);
        drawText(c, FONT_SMB, c3, r.y + 6, "Controller", gold);
        const int rowH = 15, top = r.y + 26;
        for (int i = 0; i <= kRows; ++i) {
            const int y = top + i * rowH + (i == kRows ? 6 : 0) + (i >= 5 ? 4 : 0) + (i >= 9 ? 4 : 0);
            const Rect row{r.x + 6, y - 1, r.w - 12, rowH};
            const bool sel = i == sel_;
            if (sel) {
                if (neon) {
                    c.fillRoundRect(row.x, row.y, row.w, row.h, 5, pal.selFill, 150);
                    c.frameRoundRect(row.x, row.y, row.w, row.h, 5, pal.selB, 1);
                } else {
                    c.fillRoundRect(row.x, row.y, row.w, row.h, 5, theme::panelHi, 240);
                    c.frameRoundRect(row.x, row.y, row.w, row.h, 5, theme::gold, 1);
                }
            }
            if (capture_ < 0) hits::add(row, [this, i]() { sel_ = i; });
            if (i == kRows) {
                const int w = drawText(c, FONT_SMB, c1, y, "Put every key back to the defaults", sel ? gold : ink);
                if (!keymap::isDefault()) drawText(c, FONT_XS, c1 + w + 10, y + 2, "(some keys are changed)", dim);
                continue;
            }
            const int act = kOrder[i];
            drawText(c, FONT_SM, c1, y, keymap::actName(act), sel ? gold : ink);
            if (capture_ == i) {
                const bool on = ((now - captureSince_) / 450U) % 2U == 0U;
                drawText(c, FONT_SMB, c2, y, on ? "Press a key..." : "", rgb(255, 214, 120));
                drawText(c, FONT_XS, c2 + 110, y + 2, act == keymap::Menu ? "(right click: keep it)" : "(Esc: keep it)", dim);
            } else {
                drawText(c, FONT_SM, c2, y, fitText(FONT_SM, keymap::allKeys(act), c3 - c2 - 10), keyCol);
            }
            drawText(c, FONT_SM, c3, y, kPad[i], padCol);
        }
        // the mouse, which isn't bound
        const int fy = r.y + r.h - 44;
        c.fillRect(r.x + 10, fy - 6, r.w - 20, 1, neon ? pal.frame : theme::border, 160);
        drawText(c, FONT_XS, c1, fy, "Mouse: point at a tile or row, left click = choose (A), right click = back (B), right drag moves the map,", dim);
        drawText(c, FONT_XS, c1, fy + 12, "the wheel zooms. Edge scrolling is in Options. F11 or Alt+Enter: full screen. Number keys type numbers.", dim);
        if (!note_.empty() && int32_t(noteUntil_ - now) > 0)
            drawTextAligned(c, FONT_SMB, r.x, r.y + r.h - 18, r.w, Align::Center, fitText(FONT_SMB, note_, r.w - 20),
                            noteBad_ ? rgb(255, 120, 110) : rgb(140, 230, 160));
        if (capture_ >= 0) {
            drawHintBar(c, {});
            drawText(c, FONT_SM, 12, c.height() - 22, "Press the key you want for " + std::string(keymap::actName(kOrder[capture_])), theme::dim);
        } else {
            drawHintBar(c, {{"A", "Change key"}, {"B", "Back"}});
        }
    }

private:
    static constexpr int kRows = 20;
    // the rows' order: the camera, the cursor, then the buttons
    static constexpr int kOrder[kRows] = {keymap::CamUp,  keymap::CamDown, keymap::CamLeft, keymap::CamRight, keymap::CamFast,
                                         keymap::Up,     keymap::Down,    keymap::Left,    keymap::Right,    keymap::A,
                                         keymap::B,      keymap::L1,      keymap::R1,      keymap::Start,    keymap::Menu,
                                         keymap::R2,     keymap::L2,      keymap::X,       keymap::Y,        keymap::Select};
    static constexpr const char* kPad[kRows] = {"Right stick", "Right stick", "Right stick", "Right stick", "-",
                                                "D-pad / left stick", "D-pad / left stick", "D-pad / left stick", "D-pad / left stick", "A",
                                                "B", "L1 (LB)", "R1 (RB)", "View (Start)", "Menu",
                                                "Right trigger", "Left trigger", "X", "Y", "Click a stick"};
    void note(const std::string& s, bool bad) {
        note_ = s;
        noteBad_ = bad;
        noteUntil_ = now_ + 4000;
    }
    int sel_ = 0, capture_ = -1;
    uint32_t now_ = 0, captureSince_ = 0, noteUntil_ = 0;
    std::string note_;
    bool noteBad_ = false;
};
constexpr int ControlsScene::kOrder[ControlsScene::kRows];
constexpr const char* ControlsScene::kPad[ControlsScene::kRows];

// ---------------------------------------------------------------- 0.68.0: the music player (owner: "a music player for the
// game ... where the player can put their own music in a folder so that they can play sound tracks that they want. The default
// music will be what we have currently. The title screen music should stay unchanged as well"; "only accessible at the game
// menu under options ... not accessible when viewing and playing on the game map"). Options -> Music player: what a game
// plays (the game soundtrack, My Music, both), in order or shuffled, every song ticked on or off, a song listened to here (the
// title's music comes back when you leave), the folder looked at again. audio/mixer.cpp keeps the choices.
class MusicScene : public Scene {
public:
    MusicScene() {
        folder_ = audio::musicFolder();
        build();
    }
    void update(App& app, const Input& in, uint32_t now) override {
        now_ = now;
        const int n = int(rows_.size());
        auto selectable = [&](int i) { return rows_[size_t(i)].kind != Row::Header && rows_[size_t(i)].kind != Row::Empty; };
        auto step = [&](int d, bool wrap) {  // the next row that can be chosen (the D-pad goes round; L1 / R1 stop at the ends)
            for (int k = 0, i = sel_; k < n; ++k) {
                i += d;
                if (i < 0 || i >= n) {
                    if (!wrap) return;
                    i = (i + n) % n;
                }
                if (selectable(i)) {
                    sel_ = i;
                    return;
                }
            }
        };
        if (in.pressed(Btn::Up)) step(-1, true);
        if (in.pressed(Btn::Down)) step(1, true);
        if (in.pressed(Btn::L1)) for (int k = 0; k < 10; ++k) step(-1, false);
        if (in.pressed(Btn::R1)) for (int k = 0; k < 10; ++k) step(1, false);
        if (listen_ >= 0) {  // a click on a song's listen button
            sel_ = listen_;
            listen();
            listen_ = -1;
        }
        if (in.pressed(Btn::A)) act(0);
        if (in.pressed(Btn::Left)) act(-1);
        if (in.pressed(Btn::Right)) act(1);
        if (in.pressed(Btn::X)) listen();
        if (openClick_) {  // (Windows) the folder in File Explorer
            openClick_ = false;
            if (!audio::showFolder(folder_)) {
                note_ = "Couldn't open the folder";
                noteUntil_ = now + 3000;
                noteBad_ = true;
            }
        }
        if (in.pressed(Btn::Y) || rescanClick_) {  // look in the folder again
            rescanClick_ = false;
            const int found = audio::rescanMusic();
            build();
            note_ = found == 0 ? "No MP3 files in the folder yet" : "Found " + std::to_string(found) + (found == 1 ? " song" : " songs");
            noteBad_ = found == 0;
            noteUntil_ = now + 3000;
            audio::play(audio::Sfx::Confirm);
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            if (listening_ >= 0 || audio::musicTrack() != audio::kTrackTitle) backToTitleTheme(700, 1500);
            app.pop();
        }
    }
    bool animating() const override { return true; }
    bool bareClickIsA(float, float) const override { return false; }  // (a click on empty space ticks nothing)
    void draw(App&, Canvas& c, uint32_t now) override {
        now_ = now;
        titleBackdrop(c, now);
        drawTextAligned(c, FONT_LG, 0, 10, c.width(), Align::Center, "Music player", theme::gold);
        const Rect r{12, 40, 616, 404};
        titlePanel(c, r, 12);
        const TitlePal& pal = titlePal();
        const bool neon = artTitle();
        const uint32_t ink = neon ? pal.label : theme::text, inkSel = neon ? pal.labelSel : theme::text;
        const uint32_t dim = neon ? pal.detail : theme::dim, gold = neon ? pal.top : theme::gold;
        // now playing (listening here)
        const int playing = audio::musicTrack();
        {
            const Rect np{r.x + 10, r.y + 8, r.w - 20, 40};
            std::string line = "The title screen plays its own theme. X on a song: listen to it here.";
            if (playing > audio::kTrackTitle && playing < audio::trackCount()) {
                line = "\xE2\x99\xAA " + audio::trackTitle(playing);
                const std::string by = audio::trackAlbum(playing);
                if (!by.empty()) line += "  \xC2\xB7  " + by;
            }
            drawText(c, FONT_SMB, np.x + 4, np.y + 2, fitText(FONT_SMB, line, np.w - 8), playing > audio::kTrackTitle ? gold : dim);
            const float pr = playing > audio::kTrackTitle ? audio::musicProgress() : -1.f;
            if (pr >= 0.f) {
                c.fillRect(np.x + 4, np.y + 24, np.w - 8, 4, neon ? rgb(60, 40, 90) : theme::border);
                c.fillRect(np.x + 4, np.y + 24, int(float(np.w - 8) * pr), 4, gold);
            }
        }
        // the list
        const int top = r.y + 54, rowH = 19, bottom = r.y + r.h - 40;
        const int vis = (bottom - top) / rowH;
        const int n = int(rows_.size());
        if (sel_ < scroll_) scroll_ = sel_;
        if (sel_ >= scroll_ + vis) scroll_ = sel_ - vis + 1;
        if (sel_ <= 3) scroll_ = 0;  // (the two choices and the first header stay in view at the top)
        scroll_ = std::max(0, std::min(scroll_, std::max(0, n - vis)));
        const std::vector<int> list = audio::musicPlaylist();
        for (int k = 0; k < vis && scroll_ + k < n; ++k) {
            const int i = scroll_ + k;
            const Row& row = rows_[size_t(i)];
            const int y = top + k * rowH;
            const Rect rr{r.x + 10, y, r.w - 20, rowH - 1};
            const bool sel = i == sel_;
            if (row.kind == Row::Header) {  // (a section the games don't play says so)
                const int mode = audio::musicMode();
                const bool unused = (row.track == 0 && mode == audio::kPlayMine) || (row.track == 1 && mode == audio::kPlayGame);
                const std::string head = row.text + (unused ? "  \xC2\xB7  NOT PLAYED IN GAMES" : "");
                drawText(c, FONT_XS, rr.x + 4, y + 6, head, unused ? dim : gold);
                c.fillRect(rr.x + 4 + textWidth(FONT_XS, head) + 8, y + 11, rr.w - textWidth(FONT_XS, head) - 16, 1, neon ? pal.frame : theme::border);
                continue;
            }
            if (row.kind == Row::Empty) {
                drawText(c, FONT_XS, rr.x + 4, y + 4, fitText(FONT_XS, row.text, rr.w - 8), dim);
                continue;
            }
            if (sel) selectedRow(c, rr, 6);
            const int idx = i;
            hits::add(rr, [this, idx] { sel_ = idx; });  // (pointing chooses the row; a click also presses A: tick / change)
            if (row.kind == Row::Mode || row.kind == Row::Order) {
                drawText(c, FONT_SMB, rr.x + 8, y + 2, row.kind == Row::Mode ? "In games, play:" : "Order:", sel ? inkSel : ink);
                const std::string v = row.kind == Row::Mode ? modeName(audio::musicMode()) : audio::musicShuffle() ? "Shuffled" : "In order";
                const std::string arrows = sel ? "\xE2\x97\x80  " + v + "  \xE2\x96\xB6" : v;
                drawText(c, FONT_SMB, rr.x + 150, y + 2, arrows, gold);
                continue;
            }
            // a track: its tick, title, artist, listen button
            const int t = row.track;
            const bool on = audio::trackOn(t);
            const bool counts = std::find(list.begin(), list.end(), t) != list.end();
            const Rect box{rr.x + 8, y + 3, 12, 12};
            c.frameRect(box.x, box.y, box.w, box.h, sel ? gold : dim, 1);
            if (on) c.fillRect(box.x + 3, box.y + 3, 6, 6, counts ? theme::green : dim);
            const bool here = playing == t;
            std::string title = (here ? "\xE2\x99\xAA " : "") + audio::trackTitle(t);
            const std::string by = audio::trackAlbum(t);
            const int tx = rr.x + 28, lw = rr.w - 28 - 50;
            const int titleW = std::min(textWidth(FONT_SM, title), by.empty() ? lw : lw * 3 / 5);
            drawText(c, FONT_SM, tx, y + 3, fitText(FONT_SM, title, titleW), here ? gold : on ? (sel ? inkSel : ink) : dim);
            if (!by.empty()) drawText(c, FONT_XS, tx + titleW + 10, y + 5, fitText(FONT_XS, by, lw - titleW - 10), dim);
            const Rect play{rr.x + rr.w - 44, y + 1, 40, rowH - 3};
            drawPanel(c, play, here ? rgb(60, 48, 20) : rgb(20, 26, 46), here ? theme::gold : theme::border, 4, 220);
            drawTextAligned(c, FONT_XS, play.x, play.y + 2, play.w, Align::Center, here ? "Stop" : "Listen", here ? theme::gold : theme::text);
            hits::addAction(play, [this, idx] { listen_ = idx; });
        }
        if (scroll_ > 0) drawTextAligned(c, FONT_XS, r.x, top - 11, r.w - 16, Align::Right, "\xE2\x96\xB2 more", dim);
        if (scroll_ + vis < n) drawTextAligned(c, FONT_XS, r.x, top + vis * rowH, r.w - 16, Align::Right, "\xE2\x96\xBC more", dim);
        drawScrollBar(c, Rect{r.x + r.w - 8, top, 4, vis * rowH - 2}, scroll_, vis, n, [this, vis, n](int dir) {  // (0.81.1, owner)
            sel_ = std::max(0, std::min(n - 1, sel_ + dir * std::max(1, vis - 1)));
        });
        // the folder
        const int fy = r.y + r.h - 44;
        c.fillRect(r.x + 10, fy - 4, r.w - 20, 1, neon ? pal.frame : theme::border);
        if (!note_.empty() && int32_t(noteUntil_ - now) > 0) drawText(c, FONT_SMB, r.x + 14, fy - 2, note_, noteBad_ ? theme::gold : theme::green);
        else drawText(c, FONT_XS, r.x + 14, fy, fitText(FONT_XS, "Your own music goes in this folder (MP3 files; folders inside it too):", r.w - 240), dim);
        const bool explorer = audio::canShowFolder();  // (Windows: a button opens the folder in File Explorer)
        drawText(c, FONT_XS, r.x + 14, fy + 13, fitText(FONT_XS, folder_, r.w - (explorer ? 236 : 150)), ink);
        const Rect again{r.x + r.w - 128, fy + 2, 114, 20};
        drawPanel(c, again, rgb(20, 26, 46), theme::border, 4, 220);
        drawTextAligned(c, FONT_XS, again.x, again.y + 4, again.w, Align::Center, "Look for new songs", theme::text);
        hits::addAction(again, [this] { rescanClick_ = true; });
        if (explorer) {
            const Rect open{again.x - 86, fy + 2, 80, 20};
            drawPanel(c, open, rgb(20, 26, 46), theme::border, 4, 220);
            drawTextAligned(c, FONT_XS, open.x, open.y + 4, open.w, Align::Center, "Open folder", theme::text);
            hits::addAction(open, [this] { openClick_ = true; });
        }
        drawHintBar(c, {{"A", "Tick / change"}, {"X", "Listen"}, {"Y", "Look for new songs"}, {"B", "Back"}});
    }

private:
    struct Row {
        enum Kind { Mode, Order, Header, Track, Empty } kind;
        int track = -1;
        std::string text;
    };
    static const char* modeName(int m) { return m == audio::kPlayMine ? "My Music" : m == audio::kPlayBoth ? "Both" : "Game soundtrack"; }
    void build() {
        const int keep = sel_ < int(rows_.size()) && rows_[size_t(sel_)].kind == Row::Track ? rows_[size_t(sel_)].track : -1;
        rows_.clear();
        rows_.push_back({Row::Mode, -1, ""});
        rows_.push_back({Row::Order, -1, ""});
        rows_.push_back({Row::Header, 0, "GAME SOUNDTRACK"});
        for (int t = audio::kTrackAction; t < audio::kTracks; ++t)
            if (audio::trackAvailable(t)) rows_.push_back({Row::Track, t, ""});
        int mine = 0;
        for (int t = audio::kTracks; t < audio::trackCount(); ++t) ++mine;
        rows_.push_back({Row::Header, 1, "MY MUSIC  \xC2\xB7  " + std::to_string(mine) + (mine == 1 ? " song" : " songs")});
        if (mine == 0) rows_.push_back({Row::Empty, -1, "No songs yet: put MP3 files in the folder below, then press Y (or click Look for new songs)."});
        if (const int skipped = audio::musicSkippedFiles())  // (FLAC, M4A, WAV ... aren't played)
            rows_.push_back({Row::Empty, -1, std::to_string(skipped) + (skipped == 1 ? " other music file is" : " other music files are") +
                                                 " not MP3 - only MP3 files play (convert them to MP3 to hear them)."});
        for (int t = audio::kTracks; t < audio::trackCount(); ++t) rows_.push_back({Row::Track, t, ""});
        sel_ = 0;
        for (int i = 0; i < int(rows_.size()); ++i)
            if (keep >= 0 && rows_[size_t(i)].kind == Row::Track && rows_[size_t(i)].track == keep) sel_ = i;
    }
    // A / click (0) or left / right (-1 / +1) on the chosen row
    void act(int dir) {
        if (sel_ < 0 || sel_ >= int(rows_.size())) return;
        const Row& row = rows_[size_t(sel_)];
        if (row.kind == Row::Mode) {
            audio::setMusicMode((audio::musicMode() + (dir < 0 ? 2 : 1)) % 3);
        } else if (row.kind == Row::Order) {
            audio::setMusicShuffle(!audio::musicShuffle());
        } else if (row.kind == Row::Track && dir == 0) {
            audio::setTrackOn(row.track, !audio::trackOn(row.track));
        } else {
            return;
        }
        audio::play(audio::Sfx::Toggle);
    }
    void listen() {
        if (sel_ < 0 || sel_ >= int(rows_.size()) || rows_[size_t(sel_)].kind != Row::Track) return;
        const int t = rows_[size_t(sel_)].track;
        if (audio::musicTrack() == t) {  // listening to it: back to the title's theme
            backToTitleTheme(500, 1200);
            listening_ = -1;
        } else {
            audio::musicTo(t, 400, 600);
            listening_ = t;
        }
    }
    static void backToTitleTheme(int outMs, int inMs) {  // (the title's theme; without its file, quiet as before)
        if (audio::trackAvailable(audio::kTrackTitle)) audio::musicTo(audio::kTrackTitle, outMs, inMs);
        else audio::musicFadeOut(outMs);
    }
    std::vector<Row> rows_;
    std::string folder_, note_;
    uint32_t noteUntil_ = 0, now_ = 0;
    int sel_ = 0, scroll_ = 0, listen_ = -1, listening_ = -1;
    bool rescanClick_ = false, openClick_ = false, noteBad_ = false;
};

class TestsMenuScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t) override {
        const int n = 4;
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        if (in.pressed(Btn::A)) {
            switch (sel_) {
                case 0: app.push(makeButtonTestScene()); break;
                case 1: app.push(makeDisplayTestScene()); break;
                case 2: app.push(makeSystemInfoScene(app)); break;
                case 3: app.pop(); break;
            }
        }
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        titleBackdrop(c, now);
        drawTextAligned(c, FONT_LG, 0, 40, c.width(), Align::Center, "Device tests", theme::gold);
        std::vector<MenuItem> items = {{"Button test", "Check that every button is detected"},
                                       {"Screen test", "Colours, orientation and screen edges"},
                                       {"System info", "Device details and speed benchmark"},
                                       {"Back", ""}};
        Rect menu{150, 110, 340, 4 * 40};
        titlePanel(c, Rect{menu.x - 14, menu.y - 12, menu.w + 28, menu.h + 18}, 12);
        titleMenu(c, menu, items, sel_, now, &sel_);
        drawHintBar(c, {{"A", "Open"}, {"B", "Back"}});
    }

private:
    int sel_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeTitleScene() { return std::unique_ptr<Scene>(new TitleScene()); }
std::unique_ptr<Scene> makeTestsMenuScene() { return std::unique_ptr<Scene>(new TestsMenuScene()); }
std::unique_ptr<Scene> makeOptionsScene() { return std::unique_ptr<Scene>(new OptionsScene()); }

// Campaigns / Modules (0.28.0, owner): placeholders - the entries are greyed out for now.
namespace {
class ComingSoonScene : public Scene {
public:
    ComingSoonScene(std::string title, std::string sub, std::vector<std::string> entries)
        : title_(std::move(title)), sub_(std::move(sub)), entries_(std::move(entries)) {}
    void update(App& app, const Input& in, uint32_t) override {
        if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return artTitle(); }  // (0.59.70: the Retrowave / Voidfall backdrop moves)
    void draw(App&, Canvas& c, uint32_t now) override {
        titleBackdrop(c, now);
        drawTextAligned(c, FONT_XL, 0, 40, c.width(), Align::Center, title_, theme::gold);
        drawTextAligned(c, FONT_MD, 0, 86, c.width(), Align::Center, sub_, theme::dim);
        const int n = int(entries_.size());
        Rect r{170, 140, 300, n * 44 + 20};
        titlePanel(c, r, 12);
        for (int i = 0; i < n; ++i) {
            const int y = r.y + 14 + i * 44;
            c.fillRoundRect(r.x + 12, y, r.w - 24, 36, 8, rgb(24, 28, 40));
            drawText(c, FONT_MDB, r.x + 26, y + 4, entries_[size_t(i)], rgb(90, 96, 112));
            drawText(c, FONT_XS, r.x + 26, y + 21, "Coming soon", rgb(70, 74, 90));
        }
        drawHintBar(c, {{"B", "Back"}});
    }

private:
    std::string title_, sub_;
    std::vector<std::string> entries_;
};
// Campaigns (0.30.0, owner's Big Push): each one has its backdrop across the top, and moving
// between them fades one picture into the next. None can be played yet; Twilight's Fall is
// there, greyed out.
class CampaignScene : public Scene {
public:
    struct Entry {
        const char* name;
        const char* pic;  // art/<pic>-pc.jpg / -miyoo.jpg
        const char* blurb;
        bool open;        // planned for a later update (Twilight's Fall: not planned yet); none is playable
    };
    CampaignScene() {
        entries_ = {{"Eve of the Fall", "eve_of_the_fall",
                     "The last days of the Lazax. The emperor still sits on Mecatol Rex, but the great houses are already "
                     "counting the fleets they will need when the throne falls.", true},
                    {"Fated Expedition", "fated_expedition",
                     "A fleet goes out beyond the charted lanes, and something out there has been waiting for it.", true},
                    {"Trade Wars", "trade_wars",
                     "When contracts are worth more than planets, every deal is a weapon and every partner a rival.", true},
                    {"Twilight's Fall", "twilights_fall",
                     "The crown shatters. Thunder's Edge's own way to play, where the galaxy itself breaks apart around "
                     "the last who would rule it.", false}};
        pics_.resize(entries_.size());
        tried_.assign(entries_.size(), false);
    }
    void update(App& app, const Input& in, uint32_t now) override {
        const int n = int(entries_.size());
        int prev = sel_;
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (sel_ != prev) {
            from_ = prev;
            fadeAt_ = now;
        }
        if (in.pressed(Btn::A)) toast_ = now;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return true; }
    void draw(App&, Canvas& c, uint32_t now) override {
        drawSpaceBackground(c, false);
        const int bannerH = 300;
        const float fade = fadeAt_ ? std::min(1.f, float(now - fadeAt_) / 900.f) : 1.f;
        if (fade < 1.f && from_ >= 0) drawBackdrop(c, from_, bannerH, 255);
        drawBackdrop(c, sel_, bannerH, fade < 1.f && from_ >= 0 ? int(fade * fade * (3.f - 2.f * fade) * 255.f) : 255);
        // a dark fall-off at the foot of the picture, where the text sits
        for (int k = 0; k < 60; ++k) c.fillRect(0, bannerH - 60 + k, c.width(), 1, rgb(4, 6, 14), k * 4);
        const Entry& e = entries_[size_t(sel_)];
        // 0.32.0 (owner): the campaigns stay greyed out until they can be played; the pictures keep their colour
        drawText(c, FONT_XL, 22, bannerH - 52, e.name, rgb(150, 156, 170));
        drawText(c, FONT_SM, 24, bannerH - 18, e.open ? "Campaign \xC2\xB7 coming soon" : "Not available yet", theme::dim);
        drawTextAligned(c, FONT_SMB, 0, 8, c.width() - 12, Align::Right, "CAMPAIGNS", rgb(230, 220, 200));
        // the list, and the chosen one's story
        const int ly = bannerH + 10;
        for (int i = 0; i < int(entries_.size()); ++i) {
            const Rect r{16, ly + i * 34, 220, 30};
            const bool sel = i == sel_;
            hits::add(r, [this, i, now]() {
                if (sel_ != i) {
                    from_ = sel_;
                    fadeAt_ = now;
                    sel_ = i;
                }
            });
            c.fillRoundRect(r.x, r.y, r.w, r.h, 7, sel ? rgb(30, 34, 46) : rgb(16, 20, 32), 235);
            if (sel) c.frameRoundRect(r.x, r.y, r.w, r.h, 7, rgb(110, 116, 132), 1);
            drawText(c, FONT_MDB, r.x + 12, r.y + 6, entries_[size_t(i)].name, sel ? rgb(140, 146, 160) : rgb(84, 90, 104));
        }
        drawPanel(c, Rect{250, ly, 374, 134}, theme::panel, theme::border, 10, 235);
        int ty = ly + 10;
        for (const std::string& l : wrapText(FONT_SM, e.blurb, 350)) {
            drawText(c, FONT_SM, 262, ty, l, theme::text);
            ty += 17;
        }
        if (toast_ && now - toast_ < 1800)
            drawTextAligned(c, FONT_SMB, 250, ly + 108, 374, Align::Center, "This campaign isn't playable yet.", theme::gold);
        drawHintBar(c, {{"\xE2\x86\x91\xE2\x86\x93", "Choose"}, {"B", "Back"}});
    }

private:
    void drawBackdrop(Canvas& c, int i, int bannerH, int alpha) {
        const Entry& e = entries_[size_t(i)];
        if (!e.pic[0]) {  // no picture: a dim, empty sky
            c.fillRect(0, 0, c.width(), bannerH, rgb(8, 10, 20), alpha * 3 / 4);
            return;
        }
        if (!tried_[size_t(i)]) {
            tried_[size_t(i)] = true;
            Image src;
            if (loadPicture(e.pic, src)) {
                // fitted once to the banner in real pixels (sharp on the PC picture)
                const int w = c.X(c.width()) - c.X(0), h = c.Y(bannerH) - c.Y(0);
                pics_[size_t(i)] = resizeImage(src, w, h);
            } else {
                LOG("campaigns: no picture %s", e.pic);
            }
        }
        if (pics_[size_t(i)].empty()) {
            c.fillRect(0, 0, c.width(), bannerH, rgb(20, 14, 30), alpha * 3 / 4);
            return;
        }
        if (c.fullWidth() > c.width()) {  // widescreen (0.46.0): a soft, darkened copy fills the sides
            Wide& wd = wide_[size_t(i)];
            if (wd.sides.w != c.pw() || wd.pic.w != pics_[size_t(i)].w) {
                wd.sides = blurredSides(pics_[size_t(i)], c.pw(), pics_[size_t(i)].h);
                wd.pic = fadedEdges(pics_[size_t(i)], c.S(40));
            }
            const Rect clip = c.clip();
            c.setClip(Rect{-c.viewX(), 0, c.fullWidth(), c.height()});  // (the sides are outside the centred view)
            c.pxBlit(wd.sides, 0, c.Y(0), alpha);
            c.setClip(clip);
            c.pxBlit(wd.pic, c.X(0), c.Y(0), alpha);
            return;
        }
        c.pxBlit(pics_[size_t(i)], c.X(0), c.Y(0), alpha);
    }
    // the picture blurred, darkened and stretched to w x h (behind it on a widescreen)
    static Image blurredSides(const Image& src, int w, int h) {
        const int sw = std::max(8, src.w / 8), sh = std::max(6, src.h / 8);
        Image sm = resizeImage(src, sw, sh);
        for (int pass = 0; pass < 3; ++pass) {  // a box blur, across then down
            std::vector<uint32_t> tmp = sm.px;
            auto at = [&](int x, int y) { return tmp[size_t(std::clamp(y, 0, sh - 1)) * size_t(sw) + size_t(std::clamp(x, 0, sw - 1))]; };
            for (int y = 0; y < sh; ++y)
                for (int x = 0; x < sw; ++x) {
                    int r = 0, g = 0, b = 0;
                    for (int k = -2; k <= 2; ++k) {
                        const uint32_t p = pass % 2 ? at(x, y + k) : at(x + k, y);
                        r += int((p >> 16) & 255);
                        g += int((p >> 8) & 255);
                        b += int(p & 255);
                    }
                    sm.px[size_t(y) * size_t(sw) + size_t(x)] = 0xFF000000u | uint32_t(r / 5) << 16 | uint32_t(g / 5) << 8 | uint32_t(b / 5);
                }
        }
        Image out;
        out.w = w;
        out.h = h;
        out.px.resize(size_t(w) * size_t(h));
        for (int y = 0; y < h; ++y) {  // smooth enlargement, darkened
            const float fy = std::clamp((float(y) + 0.5f) * float(sh) / float(h) - 0.5f, 0.f, float(sh - 1));
            const int y0 = int(fy), y1 = std::min(sh - 1, y0 + 1);
            const float ty = fy - float(y0);
            for (int x = 0; x < w; ++x) {
                const float fx = std::clamp((float(x) + 0.5f) * float(sw) / float(w) - 0.5f, 0.f, float(sw - 1));
                const int x0 = int(fx), x1 = std::min(sw - 1, x0 + 1);
                const float tx = fx - float(x0);
                uint32_t o = 0xFF000000u;
                for (int sh8 = 0; sh8 <= 16; sh8 += 8) {
                    auto ch = [&](int xx, int yy) { return float((sm.px[size_t(yy) * size_t(sw) + size_t(xx)] >> sh8) & 255u); };
                    const float v = (ch(x0, y0) * (1.f - tx) + ch(x1, y0) * tx) * (1.f - ty) + (ch(x0, y1) * (1.f - tx) + ch(x1, y1) * tx) * ty;
                    o |= uint32_t(std::clamp(v * 0.45f, 0.f, 255.f)) << sh8;
                }
                out.px[size_t(y) * size_t(w) + size_t(x)] = o;
            }
        }
        return out;
    }
    // the picture with its left and right edges fading out over `edge` pixels (premultiplied)
    static Image fadedEdges(const Image& src, int edge) {
        Image out = src;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const int d = std::min(x, out.w - 1 - x);
                if (d >= edge) continue;
                const float k = float(d) / float(std::max(1, edge));
                const float a = k * k * (3.f - 2.f * k);
                uint32_t& p = out.px[size_t(y) * size_t(out.w) + size_t(x)];
                uint32_t q = 0;
                for (int sh8 = 0; sh8 <= 24; sh8 += 8) q |= uint32_t(float((p >> sh8) & 255u) * a + 0.5f) << sh8;
                p = q;
            }
        return out;
    }
    struct Wide {
        Image sides, pic;
    };
    std::vector<Wide> wide_ = std::vector<Wide>(8);
    std::vector<Entry> entries_;
    std::vector<Image> pics_;
    std::vector<bool> tried_;
    int sel_ = 0, from_ = -1;
    uint32_t fadeAt_ = 0, toast_ = 0;
};
std::unique_ptr<Scene> makeCampaignScene() { return std::unique_ptr<Scene>(new CampaignScene()); }

// Modules (0.31.0, roadmap Stage 11, owner: "the ruleset changes depending on which module is active"):
// the base game and Prophecy of Kings are always in; the rest are ticked here and saved with each new game.
class ModulesScene : public Scene {
public:
    struct Mod {
        int bit;  // 0 = always on
        const char* name;
        const char* about;
        bool ready;  // can be ticked yet
    };
    ModulesScene() {
        // (0.32.1, owner: Thunder's Edge right under the base game - the official expansions first)
        mods_ = {{0, "Base game + Prophecy of Kings", "Always in: the factions, cards, tiles and rules you play now.", true},
                 {ti::kModThundersEdge, "Thunder's Edge",
                  "Its 5 factions, new systems, space stations, neutral units, the expedition, breakthroughs, the "
                  "Fracture, cards and relics. Galactic events below.", true},
                 {ti::kModCodex, "Codex updates (Omega cards)",
                  "The Codex's reworked cards (marked Omega) take the place of the originals. Thunder's Edge "
                  "comes with them, so they are in whenever it is ticked too.", true},
                 {ti::kModUncharted, "Uncharted Space",
                  "Fan-made (by the Discordant Stars team): 24 new systems with legendary planets, 30 action cards, "
                  "16 exploration cards (Star Charts add systems to the board) and 7 relics.", true},
                 {ti::kModDiscordant, "Discordant Stars",
                  "Fan-made: 34 more factions, coming in batches (the playable ones join the faction list). Faction "
                  "breakthroughs are off with it.", true},
                 {ti::kModAbsol, "Absol's Agendas",
                  "Fan-made (by Absol197): a 60-card agenda deck that replaces the normal one - reworked agendas, "
                  "new ones and Emergency Sessions. The top voter is often rewarded.", true},
                 {-1, "Galactic events (Thunder's Edge)", "", true}};  // (0.36.0) opens the list
        // (0.82.0) Monuments+: the base game's, Prophecy of Kings' and the Keleres' 25 monuments; Thunder's Edge's in 0.82.1,
        // Discordant Stars' 35 in 0.82.2 - 0.82.4 (all 66 cards)
        mods_.insert(mods_.end() - 1, Mod{ti::kModMonuments, "Monuments+",
                                          "Fan-made (Cacotopos, after Blarknob's Monuments): a monument for each faction, placed with Construction, "
                                          "each with its own ability; 3 action cards, 2 agendas, 2 secrets.",
                                          true});
        // (0.83.0) Blue Reverie: its factions one by one - (0.85.3, owner 2026-11-24 13:08: "get rid of the blue reverie box ... we'll
        // keep these as unlockables through the leagues") no box: a game has the module when any of its factions is unlocked
    }
    void update(App& app, const Input& in, uint32_t) override {
        if (!read_) {
            read_ = true;
            on_ = app.settings().getInt("modules", ti::kModCodex);
        }
        events_ = app.settings().getInt("events", 0);  // (the events list may have changed it)
        const int n = int(mods_.size());
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (in.pressed(Btn::A) || in.pressed(Btn::Left) || in.pressed(Btn::Right)) {
            const Mod& m = mods_[size_t(sel_)];
            if (m.bit < 0) {  // galactic events: their own list
                if (on_ & ti::kModThundersEdge) {
                    note_.clear();
                    app.push(makeEventsScene());
                    audio::play(audio::Sfx::Toggle);
                } else {
                    note_ = "Tick Thunder's Edge first - galactic events are part of it.";
                }
            } else if (m.bit && m.ready) {
                on_ ^= m.bit;
                app.settings().setInt("modules", on_);
                app.settings().save();
                audio::play(audio::Sfx::Toggle);
            } else if (m.bit) {
                note_ = "Not ready yet - it will be in a coming update.";
            }
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return artTitle(); }  // (0.59.70: the Retrowave / Voidfall backdrop moves)
    void draw(App&, Canvas& c, uint32_t now) override {
        titleBackdrop(c, now);
        const int n = int(mods_.size());
        // (0.82.0: eight rows with Monuments+ - the title moves up so each row keeps room for two lines of description)
        const int top = n > 8 ? -22 : n > 7 ? -18 : 0;  // (0.83.0: nine with Blue Reverie)
        drawTextAligned(c, FONT_XL, 0, 30 + top, c.width(), Align::Center, "Modules", theme::gold);
        if (n <= 8)  // (0.83.0: with nine rows the panel takes its place)
            drawTextAligned(c, FONT_SM, 0, 74 + top, c.width(), Align::Center, "Expansions and add-ons for your next new game", theme::dim);
        const int rowH = n > 8 ? 42 : n > 7 ? 44 : n > 6 ? 45 : 51;  // (0.81.0: seven rows with Absol's Agendas; eight: Monuments+)
        const Rect panel{60, n > 8 ? 48 : n > 7 ? 72 : 96, 520, n * rowH + 12};
        titlePanel(c, panel, 12);
        for (int i = 0; i < n; ++i) {
            Mod m = mods_[size_t(i)];
            const Rect r{panel.x + 10, panel.y + 6 + i * rowH, panel.w - 20, rowH - 3};
            const bool sel = i == sel_;
            hits::add(r, [this, i]() { sel_ = i; });
            if (sel) {
                selectedRow(c, r, 8);
            }
            std::string evAbout;
            if (m.bit < 0) {  // galactic events: how many are ticked
                const int ev = events_;
                int k = 0;
                for (int e = 0; e < ti::kEventCount; ++e)
                    if (((ev >> e) & 1) && ti::galacticEvent(e).done) {
                        evAbout += std::string(k ? ", " : "") + ti::galacticEvent(e).name;
                        ++k;
                    }
                evAbout = !(on_ & ti::kModThundersEdge) ? "Part of Thunder's Edge: tick it first."
                          : k ? std::to_string(k) + " on: " + evAbout
                              : "None on. Press A to choose optional rules that change the whole game.";
                m.about = evAbout.c_str();
                m.ready = (on_ & ti::kModThundersEdge) != 0;
                if (m.ready) {  // an arrow instead of a box
                    drawText(c, FONT_MDB, r.x + 14, r.y + 6, "\xE2\x96\xB6", theme::gold);
                }
            }
            const bool ticked = m.bit == 0 || (m.bit > 0 && (on_ & m.bit));
            const uint32_t box = !m.ready ? rgb(70, 76, 92) : m.bit == 0 ? theme::dim : theme::text;
            const int dy = rowH < 45 ? -5 : rowH < 51 ? -3 : 0;  // (0.81.0: tighter rows)
            // (0.82.0) eight rows of 44: the name and the two description lines kept inside the row's 41 (lines 10 apart)
            const bool tight = rowH <= 44;  // (0.83.0: and nine of 42)
            const int k = rowH < 44 ? 1 : 0;  // (0.83.0: rows of 42 - one unit higher)
            const int nameY = tight ? r.y + 1 - k / 2 : r.y + 6 + dy, l1 = tight ? r.y + 18 - k : r.y + 26 + dy - (dy ? 1 : 0),
                      l2 = tight ? r.y + 28 - k : r.y + 38 + dy - (dy ? 2 : 0), one = tight ? r.y + 21 - k : r.y + 28 + dy;
            if (m.bit >= 0) c.frameRoundRect(r.x + 12, nameY + 4, 18, 18, 4, box, 2);
            if (ticked && (m.ready || m.bit == 0)) c.fillRoundRect(r.x + 16, nameY + 8, 10, 10, 2, m.bit == 0 ? theme::dim : theme::gold);
            drawText(c, FONT_MDB, r.x + 42, nameY, m.name, !m.ready ? rgb(100, 106, 122) : sel ? theme::text : rgb(200, 206, 220));
            // (0.59.70: a long description in two lines - not drawn over a one-line one, the neon panels are see-through)
            const std::vector<std::string> lines =
                std::string(m.about).size() > 70 ? wrapText(FONT_XS, m.about, r.w - 52) : std::vector<std::string>();
            if (lines.size() <= 1)
                drawText(c, FONT_XS, r.x + 42, one, fitText(FONT_XS, m.about, r.w - 52), !m.ready ? rgb(80, 86, 100) : theme::dim);
            {
                if (lines.size() > 1) {
                    drawText(c, FONT_XS, r.x + 42, l1, lines[0], !m.ready ? rgb(80, 86, 100) : theme::dim);
                    drawText(c, FONT_XS, r.x + 42, l2, fitText(FONT_XS, lines[1], r.w - 52), !m.ready ? rgb(80, 86, 100) : theme::dim);
                }
            }
        }
        if (!note_.empty()) drawTextAligned(c, FONT_SMB, 0, panel.y + panel.h + 4, c.width(), Align::Center, note_, theme::gold);
        else {  // (the note, when there is one, takes the footnote's place)
            std::unique_ptr<TextHalo> halo(artTitle() ? new TextHalo(170) : nullptr);  // (0.59.70: over the backdrop)
            drawTextAligned(c, FONT_XS, 0, std::max(434, panel.y + panel.h + 2), c.width(), Align::Center,
                            "Games already started keep the modules they began with.", theme::faint);
        }
        drawHintBar(c, {{"A", "Tick / untick"}, {"\xE2\x86\x91\xE2\x86\x93", "Choose"}, {"B", "Back"}});
    }

private:
    std::vector<Mod> mods_;
    int sel_ = 1, on_ = ti::kModCodex, events_ = 0;
    bool read_ = false;
    std::string note_;
};

// Galactic events (0.36.0, Thunder's Edge): optional rules for the next new game, ticked one by one; the text of
// the one under the cursor is shown below the list.
class EventsScene : public Scene {
public:
    void update(App& app, const Input& in, uint32_t) override {
        if (!read_) {
            read_ = true;
            on_ = app.settings().getInt("events", 0);
        }
        const int n = ti::kEventCount;
        if (in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % n;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % n;
        if (in.pressed(Btn::A) || in.pressed(Btn::Left) || in.pressed(Btn::Right)) {
            if (ti::galacticEvent(sel_).done) {
                on_ ^= 1 << sel_;
                app.settings().setInt("events", on_);
                app.settings().save();
                audio::play(audio::Sfx::Toggle);
                note_.clear();
            } else {
                note_ = "Not ready yet - it will be in a coming update.";
            }
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) app.pop();
    }
    bool animating() const override { return artTitle(); }  // (0.59.70: the Retrowave / Voidfall backdrop moves)
    void draw(App&, Canvas& c, uint32_t now) override {
        titleBackdrop(c, now);
        drawTextAligned(c, FONT_XL, 0, 14, c.width(), Align::Center, "Galactic events", theme::gold);
        drawTextAligned(c, FONT_SM, 0, 52, c.width(), Align::Center, "Thunder's Edge's optional rules for your next new game", theme::dim);
        const int n = ti::kEventCount, rows = 7, rowH = 21;  // (0.61.0: 7 rows - the cards' own texts are long)
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
        const Rect panel{60, 74, 520, rows * rowH + 12};
        titlePanel(c, panel, 12);
        for (int k = 0; k < rows && top_ + k < n; ++k) {
            const int i = top_ + k;
            const ti::GalacticEventDef& e = ti::galacticEvent(i);
            const Rect r{panel.x + 8, panel.y + 6 + k * rowH, panel.w - 16, rowH - 2};
            const bool sel = i == sel_;
            hits::add(r, [this, i]() { sel_ = i; });
            if (sel) {
                selectedRow(c, r, 6);
            }
            const uint32_t box = e.done ? theme::text : rgb(70, 76, 92);
            c.frameRoundRect(r.x + 8, r.y + 3, 13, 13, 3, box, 2);
            if (((on_ >> i) & 1) && e.done) c.fillRoundRect(r.x + 11, r.y + 6, 7, 7, 2, theme::gold);
            drawText(c, FONT_SMB, r.x + 30, r.y + 1, e.name, !e.done ? rgb(100, 106, 122) : sel ? theme::text : rgb(200, 206, 220));
            if (!e.done) drawTextAligned(c, FONT_XS, r.x, r.y + 3, r.w - 8, Align::Right, "coming later", rgb(90, 96, 112));
        }
        if (top_ > 0) drawTextAligned(c, FONT_XS, panel.x, panel.y - 2, panel.w - 10, Align::Right, "\xE2\x96\xB2 more", theme::faint);
        if (top_ + rows < n) drawTextAligned(c, FONT_XS, panel.x, panel.y + panel.h - 12, panel.w - 10, Align::Right, "\xE2\x96\xBC more", theme::faint);
        drawScrollBar(c, Rect{panel.x + panel.w - 6, panel.y + 6, 3, rows * rowH - 2}, top_, rows, n, [this, rows, n](int dir) {  // (0.81.1, owner)
            sel_ = std::max(0, std::min(n - 1, sel_ + dir * std::max(1, rows - 1)));
        });
        // the text of the selected event
        const ti::GalacticEventDef& e = ti::galacticEvent(sel_);
        const Rect box{60, panel.y + panel.h + 8, 520, 196};
        titlePanel(c, box, 10);
        drawText(c, FONT_MDB, box.x + 12, box.y + 6, e.name, theme::gold);
        int y = box.y + 28;
        std::vector<std::string> lines = wrapText(FONT_SM, e.text, box.w - 24);
        const bool small = 28 + int(lines.size()) * 15 > box.h - 6;
        if (small) lines = wrapText(FONT_XS, e.text, box.w - 24);
        for (const std::string& line : lines) {
            if (y > box.y + box.h - (small ? 14 : 16)) break;
            drawText(c, small ? FONT_XS : FONT_SM, box.x + 12, y, line, theme::text);
            y += small ? 13 : 15;
        }
        int ticked = 0;
        for (int i = 0; i < n; ++i)
            if (((on_ >> i) & 1) && ti::galacticEvent(i).done) ++ticked;
        if (!note_.empty())
            drawTextAligned(c, FONT_SMB, 0, box.y + box.h + 4, c.width(), Align::Center, note_, theme::gold);
        else if (ticked > 2)  // 0.43.4 (owner, T34): any number may be on, with a warning past two
            drawTextAligned(c, FONT_SMB, 0, box.y + box.h + 4, c.width(), Align::Center,
                            std::to_string(ticked) + " events ticked: more than 2 at once can make for a chaotic game.", theme::red);
        drawHintBar(c, {{"A", "Tick / untick"}, {"\xE2\x86\x91\xE2\x86\x93", "Choose"}, {"B", "Back"}});
    }

private:
    int sel_ = 0, top_ = 0, on_ = 0;
    bool read_ = false;
    std::string note_;
};

std::unique_ptr<Scene> makeEventsScene() { return std::unique_ptr<Scene>(new EventsScene()); }
std::unique_ptr<Scene> makeModulesScene() { return std::unique_ptr<Scene>(new ModulesScene()); }
}  // namespace

std::unique_ptr<Scene> makeControlsScene() { return std::unique_ptr<Scene>(new ControlsScene()); }
std::unique_ptr<Scene> makeMusicScene() { return std::unique_ptr<Scene>(new MusicScene()); }

std::unique_ptr<Scene> makeComingSoonScene(const std::string& title, const std::string& sub, const std::vector<std::string>& entries) {
    return std::unique_ptr<Scene>(new ComingSoonScene(title, sub, entries));
}
