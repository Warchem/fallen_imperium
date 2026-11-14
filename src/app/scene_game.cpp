// The main game screen: galaxy map, side panel (system details / players /
// combat), event feed, the current decision, announcements of what the
// computer players do, and the in-game menu with the reference screens.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"
#include "engine/query.h"
#include "game/habits.h"
#include "game/session.h"
#include "gfx/font.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "ui/deal_ui.h"
#include "ui/hits.h"
#include "ui/play_area.h"
#include "ui/decision_ui.h"
#include "ui/cards.h"
#include "ui/codec.h"
#include "ui/speech.h"
#include "ui/dice.h"
#include "ui/icons.h"
#include "ui/info_screens.h"
#include "ui/map_view.h"
#include "ui/ground_battle.h"
#include "ui/space_battle.h"
#include "ui/tile_view.h"
#include "ui/theme.h"
#include "ui/transition.h"
#include "ui/widgets.h"

using namespace ti;

namespace {

constexpr int kMapW = 420;  // (the 640-wide layout's; the menu overlay keeps it)
// Widescreen (0.46.0): the screen's width; the side panel keeps its 220 units at the
// right edge and the map gets the rest (420 on a 640 screen)
int W() { return theme::screenW(); }
int mapW() { return theme::screenW() - 220; }
constexpr int kSideAlpha = 150;  // 0.20.0 (owner): the side panel is see-through, the map runs on under it
constexpr int kTopH = 22;
constexpr int kHintY = 452;
const char* kSpeedNames[4] = {"Slow", "Normal", "Fast", "Instant"};
// "Music: 6 ||||||...." - a volume as a number and a little bar
std::string volumeLabel(const char* what, int v) {
    std::string bar;
    for (int i = 0; i < 10; ++i) bar += i < v ? "\xE2\x80\xA2" : "\xC2\xB7";
    return std::string(what) + ": " + (v == 0 ? std::string("Off") : std::to_string(v)) + "  " + bar;
}
const char* kCardPauseNames[3] = {"wait for A", "yours wait for A", "timed"};
const Rect kWebArea{4, 70, 414, 352};  // the faction web in the menu

uint32_t pcolor(const Game& g, int p) {
    return p == kNeutral ? theme::neutral : p >= 0 && p < g.numPlayers ? theme::player[g.players[p].color % 8] : theme::text;
}

const char* phaseName(Phase ph) {
    switch (ph) {
        case Phase::Setup: return "Setup";
        case Phase::Strategy: return "Strategy phase";
        case Phase::Action: return "Action phase";
        case Phase::Status: return "Status phase";
        case Phase::Agenda: return "Agenda phase";
        case Phase::GameOver: return "Game over";
    }
    return "";
}

std::string forceText(const Force& f) {
    std::string s;
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.n[t]) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(f.n[t]) + " " + unitTypeName(UnitType(t), f.n[t] > 1);
        if (f.dmg[t]) s += " (" + std::to_string(f.dmg[t]) + " dmg)";
    }
    return s;
}

std::string systemName(const Game& g, int s) {
    if (s < 0 || s >= int(g.systems.size())) return "";
    const SystemState& sy = g.systems[s];
    if (sy.home >= 0) return playerName(g, sy.home) + " home";
    const SystemDef& d = sy.d();
    if (d.planetCount == 0) return std::string(d.name) + " (" + d.key + ")";
    return d.name;
}

int fadeAlpha(uint32_t now, uint32_t start, uint32_t until, int inMs = 150, int outMs = 250) {
    int age = int(now - start), left = int(until - now);
    if (left <= 0) return 0;
    int a = 255;
    if (age < inMs) a = age * 255 / inMs;
    if (left < outMs) a = std::min(a, left * 255 / outMs);
    return std::max(0, std::min(255, a));
}

class GameScene : public Scene {
public:
    explicit GameScene(std::unique_ptr<GameSession> s) : session_(std::move(s)) {
        ti::setCodexTexts(ti::codexOn(session_->game()), session_->game().fixes >= 4);  // 0.31.0: the Omega wording
        ti::setThundersEdgeTexts(ti::teXxchaHero(session_->game()), ti::teNaalu(session_->game()));  // 0.57.1: TE's Xxekir Grom; Z'eu
        ti::setWormholeGeneratorOriginal(!ti::codexOn(session_->game()) && session_->game().fixes >= 13);
        ti::setMagmusReactorOriginal(ti::magmusOriginal(session_->game()));
        ti::setStrategyTexts((session_->game().modules & ti::kModThundersEdge) != 0);  // 0.43.3: TE's Construction / Warfare
        intro_ = transition::pending;  // 0.28.0: the game-start transition continues here
        transition::pending = false;
        layoutMap();
        const Game& g = session_->game();
        int h = session_->seat();
        if (h >= 0 && h < g.numPlayers && g.players[h].homeSystem >= 0) map_.setCursor(g, g.players[h].homeSystem);
        else map_.setCursor(g, std::max(0, g.mecatol()));
    }

    bool animating() const override { return true; }
    bool allowsScreenRescue() const override { return false; }
    bool wide() const override { return true; }  // widescreen (0.46.0): the map and panels use the whole width
    // the map stops above the event feed so nothing important hides under it
    // (widescreen: again whenever the screen's width changes - a window made wider or narrower)
    int layoutW_ = 0;
    void layoutMap() {
        if (layoutW_ == W()) return;
        layoutW_ = W();
        map_.setViewport(Rect{0, kTopH, mapW(), kHintY - kTopH - 64});
        map_.setDrawArea(Rect{0, kTopH, W(), kHintY - kTopH});  // under the see-through side panel and feed
    }
    // Keyboard & mouse (0.27.0): WASD moves the camera while the map is on screen.
    // 0.28.2 (owner): the wheel over the decision panel (strategy cards, lists) moves through it
    // 0.58.4: while your question's list / row editor has the input (uiFocus_, set by the last update), a click on
    // nothing clickable - a map tile, a gap in the panel - does nothing instead of answering it.
    bool bareClickIsA(float, float) const override { return !(uiFocus_ && ui_ && !ui_->bareClickConfirms()); }
    bool wheelScrolls(float x, float y) const override {
        if (pop_.on) {  // 0.47.0: the wheel scrolls the screen in the window, beside it zooms the map
            const Rect r = popRect();
            return x >= float(r.x) && x < float(r.x + r.w) && y >= float(r.y) && y < float(r.y + r.h);
        }
        if (!wantsPan()) return true;
        if (!ui_ || peek_) return false;
        if (ui_->fullScreen()) return true;  // the research cards
        if (ui_->mapMode()) return false;
        // 0.29.0: the decision panel itself (move / land / pay lists ...) and the right side (0.58.4: the wheel menus')
        const Rect d = ui_->area();
        const bool inPanel = x >= float(d.x) && x < float(d.x + d.w) && y >= float(d.y) && y < float(d.y + d.h);
        return inPanel || (x >= float(mapW()) && y >= float(kTopH) && y < float(kHintY));
    }
    bool wantsPan() const override {
        return !info_ && overlay_ == Overlay::None && expanded_ < 0 && session_ && session_->passTo() < 0;
    }
    bool keysPan() const override {  // 0.43.0 (owner, P12): in an editor of quantities WASD work as the arrows
        if (ui_ && !peek_ && ui_->stepsWithKeys() && session_ && session_->humanToAnswer()) return false;
        return wantsPan();
    }
    void onQuit() override {
        if (session_ && session_->engine().started() && !session_->engine().finished()) session_->save();
    }

    void update(App& app, const Input& in, uint32_t now) override {
        uiFocus_ = false;
        if (intro_) {  // the flood draws back, GAME START!, then the music returns (0.28.0)
            if (!introStart_) introStart_ = now;
            const uint32_t t = now - introStart_;
            if (t >= transition::kReveal && !introSound_) {
                introSound_ = true;
                audio::play(audio::Sfx::GameStart);
                audio::musicTo(audio::randomGameTrack(), 100, 900);  // 0.30.0: Action or Infiltration, at random
            }
            if (t >= transition::kReveal + transition::kBanner) intro_ = false;
            return;
        }
        // 0.30.0: a game continued from the title (no flood) changes to the game's music too
        if (audio::musicTrack() == audio::kTrackTitle) audio::musicTo(audio::randomGameTrack(), 1200, 2000);
        speed_ = std::max(0, std::min(3, app.settings().getInt("ai_speed", 1)));
        session_->setSpeed(speed_);
        cardPause_ = std::max(0, std::min(2, app.settings().getInt("card_pause", 0)));
        session_->setCardPause(cardPause_);
        map_.setBorders(app.settings().getInt("map_borders", 1) != 0);
        map_.setInfoTiles(app.settings().getInt("info_tiles", 1) != 0);
        const Game& g = session_->game();
        lastNow_ = now;
        {  // (0.46.2: the Galactic Events pop-up shows the text of the event under the mouse)
            const Mouse& m = in.mouse();
            hoverX_ = m.active ? m.x : -1.f;
            hoverY_ = m.active ? m.y : -1.f;
            if (m.active) mouseSeen_ = true;  // (0.47.0: the button panel shows once a mouse is in use)
        }
        if (eventsOpen_ && (info_ || overlay_ != Overlay::None || !g.events)) eventsOpen_ = false;
        if (dragBarAndPop(app, in)) return;
        if (eventsOpen_ && (in.pressed(Btn::B) || in.pressed(Btn::Menu))) {
            eventsOpen_ = false;
            audio::play(audio::Sfx::Back);
            return;
        }

        if (session_->holding() && !heldVisible_ && !info_ && overlay_ == Overlay::None && !intro_) {
            if (!heldHiddenSince_) heldHiddenSince_ = now;
            if (now - heldHiddenSince_ > 800) {
                LOG("game: released a held %d that wasn't on screen", int(session_->heldKind()));
                session_->release();
                heldHiddenSince_ = 0;
            }
        } else {
            heldHiddenSince_ = 0;
        }
        if (pop_.on && !info_ && overlay_ == Overlay::None) pop_.on = false;  // (closed by the screen inside)
        if (pop_.on && session_->passTo() >= 0) {  // (hot-seat: the next person doesn't see the last one's screens)
            info_.reset();
            overlay_ = Overlay::None;
            pop_.on = false;
        }
        if (pop_.on) {
            updatePop(app, in, now);
            return;
        }
        if (info_) {
            if (!info_->update(in)) {
                info_.reset();
                if (fromMenu_) overlay_ = Overlay::Menu;  // 0.29.0 (owner): back to the game menu it came from
                fromMenu_ = false;
            }
            return;
        }
        if (overlay_ != Overlay::None) {
            const Overlay before = overlay_;
            updateOverlay(app, in);
            if (fromMenu_ && before != Overlay::Menu && overlay_ == Overlay::None) overlay_ = Overlay::Menu;
            if ((overlay_ == Overlay::Menu || overlay_ == Overlay::None) && !info_) fromMenu_ = false;
            return;
        }
        if (wantsPan()) {
            mouseMap(in, now);
            // a click on a play area opens it (0.43.4) - also while a question is open (it isn't a choice)
            const Mouse& m = in.mouse();
            const Rect vp = map_.viewport();
            if (m.leftPress && in.pressed(Btn::A) && expanded_ < 0 && session_->passTo() < 0 && !g.draft.active && m.x >= float(vp.x) &&
                m.x < float(vp.x + vp.w) && m.y >= float(vp.y) && m.y < float(vp.y + vp.h) && !(ui_ && ui_->fullScreen())) {
                const int what = map_.hitTest(g, m.x, m.y);
                if (what >= MapView::kPlayBase && what < MapView::kDecorBase) {
                    map_.pointAt(g, what);
                    openAtCursor(g);
                    if (info_) return;
                }
            }
        }
        if (session_->over() && !session_->presenting(now)) {
            if (overlay_ != Overlay::GameOver)  // (0.54.0) the bots remember how the people played
                habits::record(app.settings(), session_->game(), session_->engine().setup());
            overlay_ = Overlay::GameOver;
            return;
        }
        // Hot-seat (0.19.0): another person's seat is to answer - hand the device over first
        if (const int q = session_->passTo(); q >= 0) {
            if (in.pressed(Btn::A)) {
                session_->takeSeat(q);
                ui_.reset();
                uiId_ = -1;
                peek_ = false;
                const int home = g.players[size_t(q)].homeSystem;
                if (home >= 0) map_.setCursor(g, home);
            }
            return;
        }
        if (in.pressed(Btn::Menu)) {
            overlay_ = Overlay::Menu;
            menuSel_ = 0;
            menuWeb_ = false;
            webMsg_.clear();
            return;
        }
        if (in.pressed(Btn::L2)) tab_ = (tab_ + 1) % 2;

        // An exploration or relic card waits on screen until A is pressed (the
        // press is used up here so it can't also answer the next question).
        if (session_->holding()) {
            browseMap(in, now);
            if (in.pressed(Btn::A)) {
                session_->release();
                card_.until = now;
                if (combat_.ended) combat_.closeAt = now + 350;  // the aftermath was admired: on with the game
            }
            return;
        }

        // Let the game run: present what happened, let the computer players act.
        bool answering = session_->humanToAnswer();
        bool hurry = !answering && in.held(Btn::A) && expanded_ < 0 && !peek_;
        for (const Event& e : session_->step(now, hurry)) present(e, now);
        answering = session_->humanToAnswer();

        if (answering && pendingPartner_ >= 0) {  // an offer started from the menu's web
            int q = pendingPartner_;
            pendingPartner_ = -1;
            const Decision& d = session_->engine().decision();
            if (d.tag == Tag::Negotiate)
                for (const Option& o : d.options)
                    if (o.id == q && o.enabled) {
                        Answer a;
                        a.picks = {q};
                        std::string err;
                        if (session_->submit(a, &err, now)) {
                            ui_.reset();
                            return;
                        }
                    }
        }
        // The expanded tile (0.20.0): D-pad = the next tile, B = back to the map.
        if (expanded_ >= 0 && answering && (!ui_ || uiId_ != session_->engine().answerCount())) expanded_ = -1;  // your turn
        if (expanded_ >= 0) {
            browseMap(in, now);
            if (map_.cursor() < MapView::kInfoBase || map_.cursor() >= MapView::kDecorBase) expanded_ = map_.cursor();
            else map_.setCursor(g, expanded_);  // the tile view skips the info tiles
            if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::R2)) expanded_ = -1;
            return;
        }
        if (answering) {
            int id = session_->engine().answerCount();
            if (!ui_ || uiId_ != id) {
                ui_ = makeDecisionUI(g, session_->engine().decision(), map_);
                uiId_ = id;
                peek_ = false;
                const Decision& d = session_->engine().decision();
                if (d.system >= 0) map_.setCursor(g, d.system);
            }
            ui_->ageError(now);
            if (peek_) {
                browseMap(in, now);
                if (in.pressed(Btn::A)) openAtCursor(g);  // the tile, large / a faction's info (0.20.0)
                else if (in.pressed(Btn::R2) || in.pressed(Btn::B)) peek_ = false;
                return;
            }
            if (in.pressed(Btn::R2) && !ui_->mapMode()) {
                peek_ = true;
                return;
            }
            // B is back AND undo (owner, 0.19.0): inside a question it backs out (clears, skips,
            // returns to the list...); with nothing left to back out of it undoes your last step.
            if (in.pressed(Btn::B) && !ui_->backAvailable()) {
                if (session_->canUndo()) {
                    session_->undo();
                    ui_.reset();
                    uiId_ = -1;
                    combat_.open = false;
                    battle_.clear();
                    ground_.clear();
                    toast("Went back.", now, theme::cyan);
                } else {
                    toast("Can't go back: the last step is final (dice, a revealed card or another player's turn).", now,
                          theme::red);
                }
                return;
            }
            if (in.pressed(Btn::Select)) {  // the players' sheets, as while watching
                overlay_ = Overlay::Players;
                return;
            }
            uiFocus_ = true;
            if (ui_->update(in, now)) {
                std::string err;
                Answer a = ui_->answer();
                if (!session_->submit(a, &err, now)) ui_->setError(err);
                else ui_.reset();
            }
            return;
        }
        ui_.reset();
        // START skips the rest of a combat (0.22.0, owner)
        if (in.pressed(Btn::Start) && combat_.open && !combat_.fireOnly && !combat_.ended) {
            session_->skipCombat();
            combat_.open = false;
            battle_.clear();
            ground_.clear();
            toast("Combat skipped.", now, theme::cyan);
            return;
        }
        // Browsing the map while the computer players act. R2: the map view, where A opens a tile.
        if (peek_) {
            browseMap(in, now);
            if (in.pressed(Btn::A)) openAtCursor(g);
            else if (in.pressed(Btn::R2) || in.pressed(Btn::B)) peek_ = false;
            return;
        }
        if (in.pressed(Btn::A) && map_.infoPlayer() >= 0) {  // A on a faction's info tiles: its sheet
            openAtCursor(g);
            return;
        }
        if (in.pressed(Btn::R2)) {
            peek_ = true;
            return;
        }
        browseMap(in, now);
        if (in.pressed(Btn::Select)) overlay_ = Overlay::Players;
        if (in.pressed(Btn::X)) overlay_ = Overlay::Objectives;
        if (in.pressed(Btn::Y)) {
            overlay_ = Overlay::Log;
            logScroll_ = 0;
        }
    }

    void draw(App& app, Canvas& c, uint32_t now) override {
        drawGame(app, c, now);
        if (intro_) transition::drawReveal(c, introStart_ ? now - introStart_ : 0);
    }
    bool fromMenu_ = false;  // the open screen / overlay was opened from the game menu (0.29.0)
    // 0.29.0 (owner: "the game sometimes hangs until I press A"): a card or combat end held for A
    // that nothing on screen shows (e.g. a combat whose tile isn't drawn) is let go after a moment.
    bool heldVisible_ = false;
    uint32_t heldHiddenSince_ = 0;
    bool intro_ = false, introSound_ = false;
    uint32_t introStart_ = 0;
    void drawGame(App& app, Canvas& c, uint32_t now) {
        const Game& g = session_->game();
        layoutMap();
        drawSpaceBackground(c, false);
        map_.setActiveSystem(g.activeSystem);
        map_.setViewer(session_->seat());
        map_.draw(c, g, now);
        drawTopBar(c, g);
        const bool combatHold = session_->holding() && session_->heldKind() == EventKind::CombatEnd;
        if (combat_.open && combat_.ended && !combatHold && int32_t(now - combat_.closeAt) >= 0) {
            combat_.open = false;
            battle_.clear();
            ground_.clear();
        }
        // 0.30.0 (owner: Skilled Retreat's "pick a system" was hidden behind the battle): a question
        // answered on the map puts the battle away so the map shows
        const bool mapPick = ui_ && !peek_ && ui_->mapMode();
        const bool tileShown = combat_.open && (combat_.space || ground_.active()) && !combat_.fireOnly && combat_.sys >= 0 && expanded_ < 0 &&
                               !mapPick;
        if (tileShown) drawCombatTile(c, g, now, combatHold);
        heldVisible_ = combatHold && tileShown;
        bool agendaPanel = g.currentAgenda >= 0 || int32_t(now - agendaResultUntil_) < 0;  // (also Executive Order)
        {
            TextHalo halo;  // the side panel is see-through: keep its text clear of the map under it
            if (combat_.open) drawCombatPanel(c, g, now);
            else if (agendaPanel && tab_ == 0 && !peek_) drawAgendaPanel(c, g);
            else if (ui_ && !peek_ && ui_->decision().tag == Tag::StrategyPick) drawStrategyPreview(c, g, ui_->highlightedValue());
            else if (ui_ && !peek_ && leaderInPrompt(ui_->decision()) >= 0) drawLeaderPreview(c, g, leaderInPrompt(ui_->decision()));
            else if (tab_ == 0) drawSystemPanel(c, g);
            else drawPlayersPanel(c, g);
        }
        bool showUi = ui_ && !peek_;
        drawFeed(c, now, showUi);
        if (showUi && ui_->centred()) {  // (widescreen: the research cards, deals etc. are laid out for 640, centred)
            CentredView v(c);
            ui_->draw(c, now);
        } else if (showUi) {
            ui_->draw(c, now);
        }
        drawCentreBanner(c, now);
        const bool holding = session_->holding() && !combatHold &&
                             (card_.kind == 3 || card_.kind == 4 || card_.kind == 5 || card_.kind == 8 || card_.kind == 10 ||
                              card_.kind == 11 || card_.kind >= 12);
        if (holding) heldVisible_ = true;
        if (card_.kind == 20 && (holding || int32_t(now - card_.until) < 0)) drawPlanetfall(c, g, now, holding);
        else if (card_.kind == 10 && (holding || int32_t(now - card_.until) < 0)) {
            CentredView v(c);  // (widescreen: laid out for 640, centred; the dimming covers the width)
            drawReveal(c, g, now, holding);
        } else if ((card_.kind == 11 || card_.kind == 21) && (holding || int32_t(now - card_.until) < 0)) {
            CentredView v(c);
            drawBetrayal(c, g, now, holding);
        }
        else if (holding) drawHeldCard(c, g, now);
        else drawCardBanner(c, g, now);
        drawToast(c, now);
        std::vector<std::pair<std::string, std::string>> hints;
        if (holding || combatHold) {
            hints = {{"A", "Continue"}, {"D-PAD", "Look"}, {"L2", "Panel"}, {"MENU", "Menu"}};
        } else if (peek_) {
            hints = {{"D-PAD", "Look"}, {"L1/R1", "Zoom"}, {"L2", "Panel"}, {"R2", "Back to decision"}};
            drawTextShadow(c, FONT_MDB, 10, kTopH + 6, keyText("Looking at the map - press R2 to return"), theme::cyan);
        } else if (ui_) {
            hints = ui_->hints();
            if (!ui_->mapMode()) hints.push_back({"R2", "Map"});
            if (!ui_->backAvailable() && session_->canUndo()) hints.push_back({"B", "Go back"});
            hints.push_back({"MENU", "Menu"});
        } else if (combat_.open && !combat_.fireOnly && !combat_.ended) {
            hints = {{"START", "Skip combat"}, {"A", "Hold: faster"}, {"D-PAD", "Look"}, {"L2", "Panel"}, {"MENU", "Menu"}};
        } else {
            hints = {{"D-PAD", "Look"}, {"X", "Goals"}, {"Y", "Log"}, {"SELECT", "Players"}, {"L2", "Panel"},
                     {"A", "Hold: faster"}, {"MENU", "Menu"}};
        }
        if (pop_.on) hints = {{"B", "Close the window"}, {"WHEEL", "Zoom the map beside it"}};  // (0.47.0: keys go to the window)
        if (const int q = session_->passTo(); q >= 0 && overlay_ == Overlay::None && !info_) {
            CentredView v(c);  // (widescreen: laid out for 640, centred)
            drawPassScreen(c, g, q);
            return;
        }
        if (expanded_ >= 0 && overlay_ == Overlay::None && !info_) {
            CentredView v(c);
            drawExpandedTile(c, g, now);
            return;
        }
        const char* openWhat = map_.playPlayer() >= 0 ? "Cards in play" : map_.infoPlayer() >= 0 ? "Faction sheet" : "Open the tile";
        if (peek_ && !ui_) hints = {{"D-PAD", "Look"}, {"A", openWhat}, {"L1/R1", "Zoom"}, {"R2", "Back"}};
        else if (peek_) hints = {{"D-PAD", "Look"}, {"A", openWhat}, {"L1/R1", "Zoom"}, {"R2", "Back to decision"}};
        else if (!ui_ && !holding && !combatHold && map_.infoPlayer() >= 0 && !hints.empty() && hints.back().first == "MENU")
            hints.insert(hints.begin() + 1, {"A", openWhat});  // (watching: A on the info tiles / play area opens them)
        drawHintBar(c, hints);
        barShown_ = false;
        if (pop_.on && (info_ || overlay_ != Overlay::None)) {  // (0.47.0: the screen in its window, the buttons still there)
            drawPop(app, c, g, now);
            return;
        }
        // 0.58.4 (owner: "the screens pop-up doesn't hide when the codecs or mecatol rex custodian animation plays"):
        // no button panel over the planetfall, the reveal, the betrayal codecs or a full-screen question (the
        // negotiation codecs, research, the expedition)
        const bool cutscene = (card_.kind == 20 || card_.kind == 10 || card_.kind == 11 || card_.kind == 21) &&
                              (holding || int32_t(now - card_.until) < 0);
        const bool fullUi = ui_ && !peek_ && ui_->centred() && session_->humanToAnswer();
        if (overlay_ == Overlay::None && !info_) {
            if (!cutscene && !fullUi) drawBar(app, c);
            drawWaypointButton(c);  // (0.56.0)
            drawEvents(c, g);  // (0.46.2: on top of everything on the map)
        }
        CentredView v(c);  // (widescreen: the menus and screens over the map are laid out for 640, centred)
        if (overlay_ != Overlay::None) drawOverlay(app, c, g);
        if (info_) info_->draw(c, now);
    }

    // A space combat played out on the system's tile, over the map (0.20.0, owner).
    void drawCombatTile(Canvas& c, const Game& g, uint32_t now, bool hold) {
        const Rect area{0, kTopH, mapW(), kHintY - kTopH - 64};
        if (combat_.space && battle_.active() && battle_.system() == combat_.sys) {  // 0.22.0: the animated fight
            c.fillRect(area.x, area.y, area.w, area.h, rgb(4, 6, 12), 170);
            battle_.draw(c, g, area, now, hold ? keyText("A: Continue") : std::string());
            return;
        }
        if (!combat_.space && ground_.active() && ground_.planet() == combat_.planet) {  // 0.23.0: on the planet
            c.fillRect(area.x, area.y, area.w, area.h, rgb(4, 6, 12), 170);
            ground_.draw(c, g, area, now, hold ? keyText("A: Continue") : std::string());
            return;
        }
        if (!combat_.space) return;
        tileview::CombatShow cs;
        cs.attacker = combat_.pa;
        cs.defender = combat_.pb;
        cs.a = combat_.a;
        cs.b = combat_.b;
        for (const Roll& r : combat_.rolls) cs.rolls.push_back({r.player, r.dice, r.need, r.hits, r.time});
        cs.losses = combat_.losses;
        cs.ended = combat_.ended;
        cs.winner = combat_.winner;
        cs.result = combat_.ended ? (combat_.winner >= 0 ? playerName(g, combat_.winner) + " wins" : std::string("No winner")) : "";
        c.fillRect(area.x, area.y, area.w, area.h, rgb(4, 6, 12), 200);
        tileview::draw(c, g, combat_.sys, area, now, &cs);
    }

    // The expanded tile (0.20.0): the system under the cursor drawn large.
    void drawExpandedTile(Canvas& c, const Game& g, uint32_t now) {
        c.fillRect(0, kTopH, 640, kHintY - kTopH, rgb(6, 8, 16), 250);
        const int s = expanded_;
        if (const decor::Tile* t = map_.decorAt(g, s)) {  // a Thunder's Edge placeholder (0.24.0)
            drawTextShadow(c, FONT_LG, 12, kTopH + 4, decor::name(*t), t->kind == decor::ThundersEdge ? rgb(230, 200, 120) : rgb(255, 120, 170));
            drawText(c, FONT_XS, 14, kTopH + 30, decor::subtitle(g, *t), t->kind != decor::ThundersEdge && g.fractureIn ? theme::green : theme::dim);
            const Rect area{20, kTopH + 46, 600, kHintY - kTopH - 50};
            float R = std::min(float(area.w) / 2.f, float(area.h) / 1.7320508f) - 4.f;
            if (t->kind == decor::ThundersEdge) R *= 0.7f;  // (room for the winglets round it)
            decor::paint(c, *t, float(area.x + area.w / 2), float(area.y + area.h / 2), R, now, true);
            if (t->kind == decor::ThundersEdge) {  // 0.43.0 (owner, P14): the six expedition slices as winglets
                uint32_t claim[6] = {};
                for (int k = 0; k < 6; ++k) {
                    const int q = g.expedition[size_t(k)];
                    if (q >= 0 && q < g.numPlayers) claim[k] = pcolor(g, q);
                }
                decor::drawWinglets(c, float(area.x + area.w / 2), float(area.y + area.h / 2), R, claim, now);
            }
            drawHintBar(c, {{"D-PAD", "Next tile"}, {"B", "Back to the map"}});
            return;
        }
        if (s < 0 || s >= int(g.systems.size())) return;
        const SystemState& sy = g.systems[size_t(s)];
        const SystemDef& d = sy.d();
        drawTextShadow(c, FONT_LG, 12, kTopH + 4, systemName(g, s), sy.home >= 0 ? pcolor(g, sy.home) : theme::gold);
        std::string kind = std::string("Tile ") + d.key;
        if (d.anomalies & kAsteroid) kind += " \xC2\xB7 asteroid field";
        if (d.anomalies & kNebula) kind += " \xC2\xB7 nebula";
        if (d.anomalies & kSupernova) kind += " \xC2\xB7 supernova";
        if (d.anomalies & kRift) kind += " \xC2\xB7 gravity rift";
        if (sy.frontier) kind += " \xC2\xB7 frontier token";
        if (s == g.mecatol() && sy.custodians && !g.custodiansRemoved) kind += " \xC2\xB7 custodians token (6 influence)";
        drawText(c, FONT_XS, 14, kTopH + 30, kind, theme::dim);
        // a short list of who is here, on the right
        int y = kTopH + 8;
        for (int q = 0; q < g.numPlayers; ++q) {
            int ships = sy.space[size_t(q)].ships(), ground = 0;
            for (int16_t x : sy.planets) ground += g.planets[size_t(x)].units[size_t(q)].groundForces();
            if (!ships && !ground && !sy.hasToken(q)) continue;
            std::string t = playerName(g, q) + ": " + std::to_string(ships) + (ships == 1 ? " ship" : " ships");
            if (ground) t += ", " + std::to_string(ground) + " on planets";
            if (sy.hasToken(q)) t += ", token";
            drawTextAligned(c, FONT_XS, 400, y, 232, Align::Right, fitText(FONT_XS, t, 232), pcolor(g, q));
            y += 13;
        }
        tileview::draw(c, g, s, Rect{20, kTopH + 46, 600, kHintY - kTopH - 50}, now);
        drawHintBar(c, {{"D-PAD", "Next tile"}, {"B", "Back to the map"}});
    }

    // Hot-seat: covers the screen (hidden cards, secrets) until the next person takes the device.
    void drawPassScreen(Canvas& c, const Game& g, int q) {
        c.fillRect(0, 0, 640, 480, rgb(4, 6, 12), 245);
        const uint32_t pc = pcolor(g, q);
        Rect r{120, 120, 400, 200};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 14, rgb(14, 18, 30));
        c.frameRoundRect(r.x, r.y, r.w, r.h, 14, pc, 3);
        c.fillCircle(float(r.x + r.w / 2), float(r.y + 58), 34.f, rgb(10, 14, 26));
        c.ring(float(r.x + r.w / 2), float(r.y + 58), 34.f, 3.f, pc);
        drawFactionIcon(c, g.players[size_t(q)].faction, r.x + r.w / 2 - 26, r.y + 32, 52);
        drawTextAligned(c, FONT_LG, r.x, r.y + 104, r.w, Align::Center, "Pass to " + playerName(g, q), pc);
        drawTextAligned(c, FONT_SM, r.x, r.y + 140, r.w, Align::Center, "Their turn to decide. Hand them the device,", theme::text);
        drawTextAligned(c, FONT_SM, r.x, r.y + 158, r.w, Align::Center, keyText("then press A."), theme::text);
        drawHintBar(c, {{"A", "I'm " + playerName(g, q)}});
    }

private:
    enum class Overlay { None, Menu, Players, Objectives, Log, GameOver };

    void browseMap(const Input& in, uint32_t now) {
        const Game& g = session_->game();
        bool moved = false;
        if (in.pressed(Btn::Up)) moved |= map_.moveCursor(g, 0);
        if (in.pressed(Btn::Right)) moved |= map_.moveCursor(g, 1);
        if (in.pressed(Btn::Down)) moved |= map_.moveCursor(g, 2);
        if (in.pressed(Btn::Left)) moved |= map_.moveCursor(g, 3);
        if (moved) lastCursorMove_ = now;
        if (int z = in.zoom()) map_.setZoom(map_.zoom() + z);  // L1 / R1, mouse wheel
    }

    // The camera and the mouse on the map (0.27.0, owner: Hearts of Iron / Total War style):
    // WASD and a right-button drag move the camera, the wheel zooms smoothly around the
    // mouse, pointing at a tile puts the cursor there without moving the camera.
    void mouseMap(const Input& in, uint32_t now) {
        const Game& g = session_->game();
        const Mouse& m = in.mouse();
        const float dt = lastMouseTick_ ? float(std::min<uint32_t>(60u, now - lastMouseTick_)) / 1000.f : 0.016f;
        lastMouseTick_ = now;
        if (in.panX() != 0.f || in.panY() != 0.f) map_.pan(g, -in.panX() * 560.f * dt, -in.panY() * 560.f * dt);
        if (m.dragging && (m.dragX != 0.f || m.dragY != 0.f)) map_.pan(g, m.dragX, m.dragY);
        const bool overMap = m.active && m.x >= 0.f && m.x < float(mapW()) && m.y >= float(kTopH) && m.y < float(kHintY);
        if (m.wheel != 0.f && overMap && !wheelScrolls(m.x, m.y)) map_.zoomBy(g, m.wheel, m.x, m.y);
        // pointing only inside the viewport: tiles under the see-through panel / feed would move the camera (0.28.0)
        const Rect vp = map_.viewport();
        const bool inView = m.x >= float(vp.x) && m.x < float(vp.x + vp.w) && m.y >= float(vp.y) && m.y < float(vp.y + vp.h);
        if (m.moved && overMap && inView && !m.dragging) {
            int what = -1;
            if (g.draft.active) what = -1;  // the draft screen points at its own spaces
            else what = map_.hitTest(g, m.x, m.y);
            if (what >= 0 && what != map_.cursor()) {
                map_.pointAt(g, what);
                lastCursorMove_ = now;
            }
        }
        map_.tick(g, now);
    }

    // A in the map view: the tile under the cursor, large; on a faction's info tiles, its faction sheet.
    void openAtCursor(const Game& g) {
        if (const int q = map_.playPlayer(); q >= 0 && q < g.numPlayers && !playarea::of(g, q, session_->seat()).empty()) {
            info_ = makePlayAreaScreen(g, session_->seat(), q);  // the play area: its cards large (0.43.4)
            return;
        }
        const int p = map_.infoPlayer();
        if (p >= 0 && p < g.numPlayers) {
            info_ = makeFactionSheet(g, session_->seat(), p);
            return;
        }
        if (map_.cursor() >= 0 && map_.cursor() < int(g.systems.size())) expanded_ = map_.cursor();
        if (map_.decorAt(g, map_.cursor())) expanded_ = map_.cursor();  // the Thunder's Edge placeholders
    }

    void follow(int sys, uint32_t now) {
        if (sys < 0) return;
        if (lastCursorMove_ != 0 && int32_t(now - lastCursorMove_) < 3000) return;  // the player is looking around
        map_.setCursor(session_->game(), sys);
    }

    void toast(const std::string& text, uint32_t now, uint32_t color) {
        toast_ = text;
        toastColor_ = color;
        toastStart_ = now;
        toastUntil_ = now + 2600;
    }

    // ------------------------------------------------------------- presenting events
    void banner(const std::string& title, const std::string& sub, uint32_t color, uint32_t now, uint32_t ms, bool big) {
        if (ms == 0) return;
        centre_.title = title;
        centre_.sub = sub;
        centre_.color = color;
        centre_.start = now;
        centre_.until = now + ms;
        centre_.big = big;
        centre_.faction = -1;
        centre_.slide = 0;
        centre_.caption.clear();
    }
    // A turn banner with the faction's label (drawn: cards::factionLabel).
    void turnBanner(const Game& g, int p, const std::string& title, const std::string& caption, uint32_t color, uint32_t now,
                    uint32_t ms, bool big, int slide) {
        banner(title, "", color, now, ms, big);
        if (ms == 0 || p < 0 || p >= g.numPlayers) return;
        const int f = g.players[p].faction;
        if (f < 0) return;
        centre_.faction = f;
        centre_.labelColour = pcolor(g, p);
        centre_.slide = slide;
        centre_.caption = caption;
    }

    void cardBanner(int kind, int card, int player, const std::string& title, const std::string& sub, uint32_t now,
                    uint32_t ms) {
        if (ms == 0) return;
        card_.kind = kind;
        card_.card = card;
        card_.player = player;
        card_.title = title;
        card_.sub = sub;
        card_.text.clear();
        card_.other = -1;
        card_.start = now;
        card_.until = now + ms;
        card_.speaker = -1;
        card_.line.clear();
        card_.reply.clear();
        card_.laid = false;
        card_.sp = speech::Speech();
        card_.sp2 = speech::Speech();
    }

    std::string nextPlayerName() const {
        const Game& g = session_->game();
        if (g.phase != Phase::Action || g.activePlayer < 0) return "";
        std::vector<int> order = initiativeOrder(g);
        auto it = std::find(order.begin(), order.end(), int(g.activePlayer));
        if (it == order.end()) return "";
        size_t i = size_t(it - order.begin());
        for (size_t k = 1; k <= order.size(); ++k) {
            int q = order[(i + k) % order.size()];
            if (q == g.activePlayer) break;
            if (!g.players[q].passed && !g.players[q].eliminated) return q == session_->human() ? "you" : playerName(g, q);
        }
        return "";
    }

    void present(const Event& e, uint32_t now) {
        const Game& g = session_->game();
        logx::crumb("showing event %d (player %d, system %d, card %d): %.100s", int(e.kind), e.player, e.system, e.card, e.text.c_str());
        // 0.30.0: an event held for A is shown even when it has no time of its own (your drawn action
        // cards, public objectives scored) - before, it was skipped and the watchdog let it go unseen
        const uint32_t d = session_->holding() ? std::max<uint32_t>(session_->durationOf(e), 1500) : session_->durationOf(e);
        if (d == 0) return;  // "Instant" speed: no announcements
        const uint32_t fx = d * 13 / 10;
        const int me = session_->human();
        const uint32_t pc = pcolor(g, e.player);
        switch (e.kind) {
            case EventKind::TurnStart:
                if (e.player == me) turnBanner(g, e.player, "Your turn", "YOUR TURN", theme::cyan, now, std::max<uint32_t>(fx, 1100), true, 1);
                else turnBanner(g, e.player, playerName(g, e.player) + "'s turn", "TURN BEGINS", pc, now, fx, false, 1);
                break;
            case EventKind::TurnEnd: {
                bool passed = e.text.find("passes") != std::string::npos;
                std::string who = e.player == me ? "You" : playerName(g, e.player);
                std::string t = who + (passed ? (e.player == me ? " pass" : " passes") : (e.player == me ? " end your turn" : " ends turn"));
                turnBanner(g, e.player, t, passed ? (e.player == me ? "YOU PASS" : "PASSES") : "TURN ENDS", pc, now, fx, false, 2);
                break;
            }
            case EventKind::RoundEnd: banner(e.text, "", theme::gold, now, fx, true); break;
            case EventKind::Phase: {
                std::string t = e.text, sub;
                if (t.rfind("--- ", 0) == 0) t = t.substr(4, t.size() > 8 ? t.size() - 8 : 0);
                if (t.rfind("New public objective: ", 0) == 0) {
                    sub = t.substr(22);
                    size_t dash = sub.find(" - ");
                    if (dash != std::string::npos) sub = sub.substr(0, dash);
                    t = "New public objective";
                }
                if (t.size() > 70) {
                    sub = t;
                    t = "";
                }
                banner(t, sub, theme::gold, now, fx, t.size() < 24 && sub.empty());
                break;
            }
            case EventKind::StrategyPick:
                cardBanner(0, e.card, e.player, strategyName(e.card), e.player == me ? "You take it" : playerName(g, e.player) + " takes it",
                           now, fx);
                break;
            case EventKind::ActionChosen:
                if (e.card >= 0 && e.card < kStrategyCards)
                    cardBanner(0, e.card, e.player, strategyName(e.card),
                               (e.player == me ? std::string("You play") : playerName(g, e.player) + " plays") + " it", now, fx);
                break;
            case EventKind::Activate:
                cardBanner(1, -1, e.player, "Tactical action", "activates " + systemName(g, e.system), now, fx);
                map_.flash(e.system, e.hostile ? rgb(255, 40, 40) : pc, now, e.hostile ? std::max<uint32_t>(fx, 2000) : fx,
                           e.hostile);
                follow(e.system, now);
                break;
            case EventKind::Move:
                for (int16_t f : e.from) map_.arrow(f, e.system, pc, now, fx);
                map_.flash(e.system, pc, now, fx);
                follow(e.system, now);
                break;
            case EventKind::Land:
            case EventKind::Control: map_.flash(e.system, pc, now, fx); break;
            case EventKind::ObjectiveScored:  // an objective scored, shown large until A (owner, 0.16.0; publics 0.28.0)
                if (session_->holding() && e.count >= 0)
                    cardBanner(12, e.count, e.player, kObjectives[e.count].name,
                               e.player == me ? std::string("You score it") : playerName(g, e.player) + " scores it", now, fx);
                break;
            case EventKind::CardDrawn:  // 0.29.0: your new action card, large (only the drawer's own)
                if (session_->holding() && e.count >= 0) cardBanner(17, e.count, e.player, kActionCards[e.count].name, "New in your hand", now, fx);
                break;
            case EventKind::ObjectiveRevealed:  // 0.29.0: a new public objective, large
                if (session_->holding() && e.count >= 0) cardBanner(18, e.count, -1, kObjectives[e.count].name, "Revealed", now, fx);
                break;
            case EventKind::RelicUsed:
                cardBanner(session_->holding() ? 13 : 5, e.card, e.player, kRelics[e.card].name,
                           (e.player == me ? std::string("You ") : playerName(g, e.player) + " ") + e.label + " it", now,
                           std::max<uint32_t>(fx, 1800));
                break;
            case EventKind::ComponentUsed: {
                const size_t nl = e.label.find('\n');
                std::string name = e.label.substr(0, nl);
                for (const char* pre : {"Relic fragments: ", "Technology: ", "Faction: "})
                    if (name.compare(0, std::strlen(pre), pre) == 0) name = name.substr(std::strlen(pre));
                cardBanner(session_->holding() ? 15 : 7, -1, e.player, name,
                           e.player == me ? std::string("You use it") : playerName(g, e.player) + " uses it", now, std::max<uint32_t>(fx, 1800));
                card_.text = nl == std::string::npos ? std::string() : e.label.substr(nl + 1);
                break;
            }
            case EventKind::TilePlaced:  // galaxy setup: the new tile lights up in the player's colour
                audio::play(audio::Sfx::Thud, 0.9f);  // 0.28.0 (owner): a thud as it lands
                map_.flash(e.system, pc, now, fx * 2);
                follow(e.system, now);
                break;
            case EventKind::Produce:
                map_.flash(e.system, theme::green, now, fx);
                follow(e.system, now);
                break;
            case EventKind::Secondary:
                cardBanner(0, e.card, e.player, strategyName(e.card),
                           (e.player == me ? std::string("You follow") : playerName(g, e.player) + " follows"), now, fx);
                break;
            case EventKind::Research: {
                std::string tech = e.text;
                size_t k = tech.find(" researches ");
                if (k != std::string::npos) tech = tech.substr(k + 12);
                else if ((k = tech.find(" gains ")) != std::string::npos) tech = tech.substr(k + 7);
                if (!tech.empty() && tech.back() == '.') tech.pop_back();
                int techId = -1;
                for (int t = 0; t < kTechCount; ++t)
                    if (tech == kTechs[t].name) techId = t;
                cardBanner(2, techId, e.player, tech, e.player == me ? "You research it" : playerName(g, e.player) + " researches",
                           now, fx);
                break;
            }
            case EventKind::CombatStart:
                combat_ = Combat();
                combat_.open = true;
                combat_.space = e.space;
                combat_.sys = e.system;
                combat_.planet = e.planet;
                combat_.pa = e.player;
                combat_.pb = e.other;
                combat_.a = e.a;
                combat_.b = e.b;
                combat_.title = e.space ? "Space combat" : "Ground combat";
                combat_.where = e.space ? systemName(g, e.system) : std::string(kPlanets[g.planets[e.planet].def].name);
                battle_.clear();
                ground_.clear();
                if (e.space) battle_.start(g, e.system, e.player, e.other, e.a, e.b, now, session_->speedFactor());
                else if (e.planet >= 0) ground_.start(g, e.system, e.planet, e.player, e.other, e.a, e.b, now, session_->speedFactor());
                map_.flash(e.system, rgb(255, 40, 40), now, fx, true);
                follow(e.system, now);
                break;
            case EventKind::Dice: {
                if (!combat_.open || combat_.ended) {
                    combat_ = Combat();
                    combat_.open = true;
                    combat_.fireOnly = true;
                    combat_.sys = e.system;
                    // "Space cannon at Sol" -> title "Space cannon", where "Sol's ships in <system>"
                    std::string l = e.label, target;
                    for (const char* cut : {" (", " at ", " of "}) {
                        size_t sp = l.find(cut);
                        if (sp == std::string::npos) continue;
                        if (std::strcmp(cut, " (") != 0) target = l.substr(sp + std::strlen(cut));
                        l = l.substr(0, sp);
                    }
                    combat_.title = l;
                    combat_.where = (target.empty() ? "" : target + " \xC2\xB7 ") + systemName(g, e.system);
                    combat_.ended = true;
                }
                Roll r;
                r.player = e.player;
                r.label = e.label;
                r.dice = e.dice;
                r.need = e.need;
                r.hits = e.hits;
                r.time = now;
                combat_.rolls.push_back(r);
                if (combat_.rolls.size() > 4) combat_.rolls.erase(combat_.rolls.begin());
                if (battle_.active() && e.system == battle_.system() && !combat_.fireOnly) battle_.roll(e.player, e.dice, e.need, e.hits, now, d);
                if (ground_.active() && e.system == combat_.sys && !combat_.fireOnly) ground_.roll(e.player, e.dice, e.need, e.hits, now, d);
                if (combat_.fireOnly) combat_.closeAt = now + d + 1800;
                break;
            }
            case EventKind::Damaged:
            case EventKind::Destroyed: {
                map_.flash(e.system, rgb(255, 60, 40), now, fx);
                if (combat_.open && !combat_.fireOnly && e.system == combat_.sys && e.unit >= 0 &&
                    (combat_.space ? e.planet < 0 : e.planet == combat_.planet)) {
                    Force* f = e.player == combat_.pa ? &combat_.a : e.player == combat_.pb ? &combat_.b : nullptr;
                    if (battle_.active() && combat_.space) {
                        if (e.kind == EventKind::Damaged) battle_.damaged(e.player, UnitType(e.unit), e.count, now);
                        else battle_.destroyed(e.player, UnitType(e.unit), e.count, now);
                    }
                    if (ground_.active() && !combat_.space) {
                        if (e.kind == EventKind::Damaged) ground_.damaged(e.player, UnitType(e.unit), e.count, now);
                        else ground_.destroyed(e.player, UnitType(e.unit), e.count, now);
                    }
                    if (f) {
                        if (e.kind == EventKind::Damaged) f->dmg[e.unit] = uint8_t(f->dmg[e.unit] + e.count);
                        else f->remove(UnitType(e.unit), e.count, true);
                        if (e.kind == EventKind::Destroyed) combat_.losses.push_back({e.player, e.unit, e.count, now});
                    }
                    combat_.lastLoss = e.text;
                }
                break;
            }
            case EventKind::CombatEnd:
                if (combat_.open) {
                    combat_.ended = true;
                    combat_.winner = e.player;
                    combat_.result = e.text;
                    combat_.closeAt = now + d + 1500;
                    const std::string won = e.player >= 0 ? (e.player == me ? std::string("You win!") : playerName(g, e.player) + " wins")
                                                          : std::string("No winner");
                    battle_.end(e.player, won, now);
                    ground_.end(e.player, won, now);
                }
                break;
            case EventKind::Retreat:
                if (!e.from.empty()) map_.arrow(e.system, e.from[0], pc, now, fx);
                if (combat_.open) combat_.lastLoss = e.text;
                if (battle_.involves(e.player)) battle_.retreat(e.player, now);
                break;
            case EventKind::Score:
                banner(e.player == me ? "You score!" : playerName(g, e.player) + " scores", e.text, theme::gold, now, fx, false);
                break;
            case EventKind::Eliminated: banner(e.text, "", theme::red, now, fx, false); break;
            case EventKind::Warning: toast(e.text, now, rgb(255, 200, 120)); break;
            case EventKind::ActionCard:
                cardBanner(3, e.card, e.player, kActionCards[e.card].name,
                           e.player == me ? "You play it" : playerName(g, e.player) + " plays it", now, std::max<uint32_t>(fx, 1200));
                break;
            case EventKind::Agenda:
                agendaResult_.clear();
                if (session_->holding()) {  // shown large until A (owner, 0.16.0)
                    cardBanner(8, e.card, -1, kAgendas[e.card].name, "Agenda phase: up for a vote", now, std::max<uint32_t>(fx, 2500));
                    break;
                }
                banner(std::string("Agenda: ") + kAgendas[e.card].name, kAgendas[e.card].law ? "A law - it stays in play if passed" : "A directive - a one-time effect",
                       rgb(130, 170, 255), now, fx, false);
                break;
            case EventKind::Vote: break;
            case EventKind::AgendaResult:
                agendaResult_ = e.text;
                agendaResultUntil_ = now + fx + 2500;
                if (session_->holding()) {  // the ruling, until A (owner, 0.16.0)
                    const size_t k = e.text.find(": ");
                    cardBanner(14, e.card, e.player, kAgendas[e.card].name,
                               e.player >= 0 ? (e.player == me ? std::string("You are elected") : playerName(g, e.player) + " is elected") : "",
                               now, std::max<uint32_t>(fx, 2600));
                    card_.text = k == std::string::npos ? e.text : e.text.substr(k + 2);
                    break;
                }
                banner(kAgendas[e.card].name, e.text.substr(e.text.find(": ") + 2), rgb(130, 170, 255), now, fx, false);
                break;
            case EventKind::Explore: {
                std::string where = e.planet >= 0 ? std::string(kPlanets[g.planets[e.planet].def].name) : systemName(g, e.system);
                cardBanner(4, e.card, e.player, kExploreCards[e.card].name,
                           (e.player == me ? std::string("You explore ") : playerName(g, e.player) + " explores ") + where, now,
                           std::max<uint32_t>(fx, 1500));
                map_.flash(e.system, rgb(150, 200, 230), now, fx);
                follow(e.system, now);
                break;
            }
            case EventKind::Relic:
                cardBanner(5, e.card, e.player, kRelics[e.card].name,
                           e.player == me ? std::string("You gain it") : playerName(g, e.player) + " gains it", now,
                           std::max<uint32_t>(fx, 2000));
                break;
            case EventKind::Leader: {
                static const char* kType[3] = {"agent", "commander", "hero"};
                const LeaderDef& L = kLeaders[e.card];
                std::string who = e.player == me ? std::string("Your ") : playerName(g, e.player) + "'s ";
                bool unlock = e.label.find("unlocked") != std::string::npos;
                cardBanner(session_->holding() && !unlock ? 16 : 6, e.card, e.player, L.name,
                           who + kType[L.type] + (unlock ? " is unlocked" : " is used"), now, std::max<uint32_t>(fx, 1800));
                break;
            }
            case EventKind::AgendaPeek:
                cardBanner(8, e.card, e.player, kAgendas[e.card].name, e.label, now, std::max<uint32_t>(fx, 2500));
                break;
            case EventKind::Ability:
                cardBanner(7, -1, e.player, e.label, e.player == me ? std::string("You use it") : playerName(g, e.player) + " uses it", now,
                           std::max<uint32_t>(fx, 1400));
                card_.text = e.text;
                if (e.system >= 0) map_.flash(e.system, pc, now, fx);
                break;
            case EventKind::Deal: {
                // 0.57.0: a promise broken to or by a person - the betrayal codec (waits for A like a betrayal);
                // 0.58.4 (owner, L4): between computer players too
                const bool mineBroken = e.card == kDealBroken && (session_->isHumanSeat(e.player) || session_->isHumanSeat(e.other));
                const bool brokenMine = e.card == kDealBroken && session_->speed() != 3;  // (Instant: the old banner)
                cardBanner(brokenMine ? 21 : 9, e.card, e.player, "", "", now, std::max<uint32_t>(fx, brokenMine ? 4500 : 1500));
                card_.other = e.other;
                std::string t = e.text;
                size_t k = t.find(" make a deal. ");
                if (k != std::string::npos) t = t.substr(k + 14);
                else if ((k = t.find(": ")) != std::string::npos && e.card != kDealBroken) t = t.substr(k + 2);
                card_.text = t;
                // the comms strip (0.57.0): who speaks and what they say - display only, chosen by a hash
                int spk = -1, lis = -1;
                speech::Cat cat = speech::Cat::Offer;
                switch (e.card) {
                    case kDealOffered: spk = e.player, lis = e.other, cat = speech::Cat::Offer; break;
                    case kDealMade: spk = e.other, lis = e.player, cat = speech::Cat::Accept; break;  // (the one who said yes)
                    case kDealDeclined: spk = e.player, lis = e.other, cat = speech::Cat::Decline; break;
                    case kDealCountered: spk = e.player, lis = e.other, cat = speech::Cat::Counter; break;
                    case kDealBroken: spk = e.player, lis = e.other, cat = speech::Cat::Betray; break;
                    default: break;
                }
                card_.seed = speech::seedOf(e.text, uint32_t(g.round) * 131U + g.seed);
                if (spk >= 0 && spk < g.numPlayers && lis >= 0 && lis < g.numPlayers) {
                    if (brokenMine || !g.players[spk].human) {
                        card_.speaker = spk;
                        card_.line = speech::pick(g.players[spk].faction, g.players[lis].faction, cat, card_.seed);
                    }
                    if (brokenMine) {
                        card_.reply = speech::pick(g.players[lis].faction, g.players[spk].faction, speech::Cat::Betrayed, card_.seed >> 3);
                        stretchForExchange();
                    }
                }
                // 0.58.4 (owner: "I can't read the bot text fast enough when it's talking to another player during
                // its action"): a bot's line on the deal card gets time to type out and be read, and the game waits
                if (!brokenMine && !card_.line.empty() && card_.kind == 9) {
                    const uint32_t chars = uint32_t(card_.line.size());
                    const float k = std::max(0.6f, session_->speedFactor());
                    const uint32_t need = 250U + chars * 1000U / 80U + uint32_t(float(1400U + chars * 22U) * k);
                    if (int32_t(card_.start + need - card_.until) > 0) card_.until = card_.start + need;
                    session_->extendShow(card_.until);
                }
                if (mineBroken && !brokenMine)
                    banner(e.player == me ? std::string("You break a promise") : playerName(g, e.player) + " breaks a promise to you",
                           e.text.substr(std::min(e.text.size(), e.text.find('(') == std::string::npos ? e.text.size() : e.text.find('('))),
                           theme::red, now, fx, false);
                break;
            }
            case EventKind::Reveal:
                cardBanner(10, -1, -1, "", "", now, std::max<uint32_t>(fx, 5000));
                break;
            case EventKind::Planetfall:  // 0.24.0: the custodians token is gone - the Council is summoned
                cardBanner(20, -1, e.player, "", "", now, std::max<uint32_t>(fx, 6500));
                break;
            case EventKind::Betrayal: {
                cardBanner(11, e.card, e.player, e.label, "", now, std::max<uint32_t>(fx, 4500));
                card_.other = e.other;
                size_t k = e.text.find("! ");
                card_.text = k == std::string::npos ? e.text : e.text.substr(k + 2);
                if (e.player >= 0 && e.player < g.numPlayers && e.other >= 0 && e.other < g.numPlayers) {  // 0.57.0: the exchange
                    card_.seed = speech::seedOf(e.text, uint32_t(g.round) * 131U + g.seed);
                    card_.speaker = e.player;
                    card_.line = speech::pick(g.players[e.player].faction, g.players[e.other].faction, speech::Cat::Betray, card_.seed);
                    card_.reply = speech::pick(g.players[e.other].faction, g.players[e.player].faction, speech::Cat::Betrayed, card_.seed >> 3);
                    stretchForExchange();
                }
                if (e.system >= 0) map_.flash(e.system, theme::red, now, 2400, true);
                break;
            }
            case EventKind::Log: break;
            case EventKind::Note:  // 0.29.0: feed lines outside the game log
                if (e.system >= 0) map_.flash(e.system, pc, now, fx);
                break;
        }
    }

    // ------------------------------------------------------------- banners
    void drawCentreBanner(Canvas& c, uint32_t now) {
        if (centre_.until == 0 || int32_t(now - centre_.until) >= 0) return;
        int a = fadeAlpha(now, centre_.start, centre_.until);
        if (a <= 0) return;
        if (centre_.faction >= 0 && drawLabelBanner(c, now, a)) return;
        // the biggest font the title fits in (long titles wrap onto two lines)
        const FontData* tf = centre_.big ? &FONT_XL : &FONT_LG;
        if (textWidth(*tf, centre_.title) > mapW() - 24) tf = &FONT_LG;
        std::vector<std::string> titleLines;
        if (!centre_.title.empty()) {
            if (textWidth(*tf, centre_.title) > mapW() - 24) tf = &FONT_MDB;
            titleLines = wrapText(*tf, centre_.title, mapW() - 24);
            if (titleLines.size() > 2) titleLines.resize(2);
        }
        std::vector<std::string> subLines = wrapText(FONT_SM, centre_.sub, mapW() - 40);
        if (subLines.size() > 3) subLines.resize(3);
        int h = int(titleLines.size()) * (tf->lineHeight + 4) + (titleLines.empty() ? 0 : 2) + int(subLines.size()) * 16 + 18;
        int y = kTopH + (kHintY - kTopH - 64) / 2 - h / 2;
        c.fillRect(0, y, mapW(), h, rgb(4, 6, 14), a * 215 / 255);
        c.hline(0, mapW() - 1, y, centre_.color, a);
        c.hline(0, mapW() - 1, y + h - 1, centre_.color, a);
        // a light sweep across the band while it appears
        int age = int(now - centre_.start);
        if (age < 500) {
            int sx = age * (mapW() + 120) / 500 - 60;
            c.fillRect(std::max(0, sx - 30), y + 1, 60, h - 2, centre_.color, 40);
        }
        int ty = y + 9;
        for (const std::string& l : titleLines) {
            drawTextAligned(c, *tf, 0, ty, mapW(), Align::Center, l, centre_.color, a);
            ty += tf->lineHeight + 4;
        }
        if (!titleLines.empty()) ty += 2;
        for (const std::string& l : subLines) {
            drawTextAligned(c, FONT_SM, 0, ty, mapW(), Align::Center, l, theme::text, a);
            ty += 16;
        }
    }

    // Turn begins / ends: the faction's label slides in from the left (begins)
    // or out to the right (ends), with a caption in the player's colour.
    bool drawLabelBanner(Canvas& c, uint32_t now, int a) {
        struct {
            int w, h;
        } lab{300, 50}, *label = &lab;
        const int h = label->h + 38;
        const int y = kTopH + (kHintY - kTopH - 64) / 2 - h / 2;
        c.fillRect(0, y, mapW(), h, rgb(4, 6, 14), a * 215 / 255);
        c.hline(0, mapW() - 1, y, centre_.color, a);
        c.hline(0, mapW() - 1, y + h - 1, centre_.color, a);
        const int age = int(now - centre_.start), life = std::max(1, int(centre_.until - centre_.start));
        const int home = (mapW() - label->w) / 2;
        int x = home;
        if (centre_.slide == 1) {  // ease in from the left over 280 ms
            float t = std::min(1.f, float(age) / std::min(280.f, life * 0.45f));
            t = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
            x = int(-float(label->w) + (home + label->w) * t);
        } else if (centre_.slide == 2) {  // hold, then slip away to the right
            float start = life * 0.6f;
            if (age > start) {
                float t = std::min(1.f, (age - start) / std::max(1.f, life - start));
                x = home + int((mapW() - home) * t * t);
            }
        }
        c.setClip(Rect{0, y + 1, mapW(), h - 2});
        cards::factionLabel(c, centre_.faction, centre_.labelColour, x, y + 6, label->w, label->h, a);
        c.resetClip();
        // a light sweep across the band while it appears
        if (age < 500) {
            int sx = age * (mapW() + 120) / 500 - 60;
            c.fillRect(std::max(0, sx - 30), y + 1, 60, h - 2, centre_.color, 30);
        }
        drawTextAligned(c, FONT_MDB, 0, y + label->h + 10, mapW(), Align::Center, centre_.caption, centre_.color, a);
        return true;
    }

    // The icon for a card banner's header (exploration decks, relics).
    static const char* cardIcon(int kind, int card) {
        if (kind == 5) return "relic";
        if (kind == 4 && card >= 0) {
            static const char* k[4] = {"cultural", "hazardous", "industrial", "frontier"};
            return k[int(kExploreCards[card].type) & 3];
        }
        return nullptr;
    }
    // The same icon at both ends of a 26-pixel header bar.
    static void headerIcons(Canvas& c, Rect r, const char* name, int a, int size = 20) {
        if (!name) return;
        icons::draw(c, name, r.x + 8, r.y + 7, size, a);
        icons::draw(c, name, r.x + r.w - 8 - size, r.y + 7, size, a);
    }

    void drawCardBanner(Canvas& c, const Game& g, uint32_t now) {
        if (card_.until == 0 || int32_t(now - card_.until) >= 0) return;
        int a = fadeAlpha(now, card_.start, card_.until, 120, 300);
        if (a <= 0) return;
        int age = int(now - card_.start);
        int slide = age < 200 ? (200 - age) * 220 / 200 : 0;
        Rect r{mapW() + 10 + slide, 300, 204, 142};
        const bool talk = card_.kind == 9 && card_.speaker >= 0 && card_.speaker < g.numPlayers && !card_.line.empty();
        if (talk) r = Rect{mapW() + 10 + slide, 262, 204, 180};  // (0.57.0: the comms strip - portrait and words)
        bool blinkOn = age < 2000 ? ((age / 250) % 2 == 0) : true;
        static const uint32_t kDeckCol[4] = {rgb(80, 130, 220), rgb(210, 80, 70), rgb(80, 180, 100), rgb(150, 130, 190)};
        uint32_t base = card_.kind == 0 ? strategyCardColor(card_.card)
                        : card_.kind == 2 ? (card_.card >= 0 ? techColor(card_.card) : rgb(70, 160, 220))
                        : card_.kind == 3 ? rgb(210, 70, 60)
                        : card_.kind == 4 ? kDeckCol[int(kExploreCards[card_.card].type)]
                        : card_.kind == 5 ? rgb(200, 160, 60)
                        : card_.kind == 6 ? rgb(170, 120, 220)
                        : card_.kind == 8 ? agendaColor(card_.card)
                        : card_.kind == 9 ? dealColor(card_.card)
                                          : pcolor(g, card_.player);
        // strategy and technology cards on the owner's templates
        if (card_.kind == 0 || (card_.kind == 2 && card_.card >= 0)) {
            const int cw = card_.kind == 0 ? 200 : 196;
            const int ch = card_.kind == 0 ? cards::strategyHeight(cw) : cw * 494 / 759;
            const int cx = r.x + (r.w - cw) / 2, cy = r.y + 2;
            c.fillRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 12, rgb(0, 0, 0), a * 150 / 255);
            if (blinkOn && age < 2000) c.fillRoundRect(cx - 4, cy - 4, cw + 8, ch + 8, 10, shade(base, 1.2f), a * 90 / 255);
            if (card_.kind == 0) cards::strategy(c, card_.card, cx, cy, cw, a);
            else cards::tech(c, card_.card, Rect{cx, cy, cw, ch}, a);
            uint32_t who = card_.player >= 0 ? pcolor(g, card_.player) : theme::text;
            c.fillRoundRect(r.x, cy + ch + 2, r.w, 18, 6, rgb(6, 8, 16), a * 230 / 255);
            drawTextAligned(c, FONT_SM, r.x, cy + ch + 3, r.w, Align::Center, fitText(FONT_SM, card_.sub, r.w - 8), who, a);
            return;
        }
        c.fillRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 12, rgb(0, 0, 0), a * 150 / 255);
        c.fillRoundRect(r.x, r.y, r.w, r.h, 10, shade(base, 0.28f), a);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 10, blinkOn ? shade(base, 1.35f) : shade(base, 0.7f), blinkOn ? 4 : 2, a);
        if (card_.kind == 0) {
            // strategy card: number in a circle, name, who
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 34, 8, shade(base, 0.75f), a);
            c.fillCircle(float(r.x + 26), float(r.y + 21), 14.f, rgb(10, 12, 20), a);
            c.ring(float(r.x + 26), float(r.y + 21), 14.f, 2.f, shade(base, 1.4f), a);
            drawTextAligned(c, FONT_LG, r.x + 12, r.y + 9, 28, Align::Center, std::to_string(card_.card + 1), theme::text, a);
            drawText(c, FONT_XS, r.x + 48, r.y + 13, "STRATEGY CARD", rgb(20, 20, 20), a);
            drawTextAligned(c, FONT_LG, r.x, r.y + 52, r.w, Align::Center, fitText(FONT_LG, card_.title, r.w - 16), theme::text, a);
        } else if (card_.kind == 1) {
            cards::commandToken(c, g.players[card_.player].faction, base, r.x + r.w / 2, r.y + 40, 62, a);
            drawTextAligned(c, FONT_MDB, r.x, r.y + 76, r.w, Align::Center, "TACTICAL ACTION", theme::text, a);
        } else if (card_.kind == 4 || card_.kind == 5) {
            std::string head = card_.kind == 5 ? "RELIC"
                                               : std::string(exploreTypeName(kExploreCards[card_.card].type)) + " exploration";
            for (char& ch : head) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, shade(base, 0.7f), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, head, theme::text, a);
            headerIcons(c, r, cardIcon(card_.kind, card_.card), a);
            auto lines = wrapText(FONT_MDB, card_.title, r.w - 16);
            int y = r.y + 36;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_MDB, r.x, y, r.w, Align::Center, lines[k], theme::text, a);
                y += 19;
            }
            auto text = wrapText(FONT_XS, card_.kind == 5 ? relicText(card_.card) : exploreText(card_.card), r.w - 16);
            for (size_t k = 0; k < text.size() && k < 3; ++k) {
                drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, text[k], rgb(225, 220, 205), a);
                y += 12;
            }
        } else if (card_.kind == 6 || card_.kind == 7) {
            static const char* kType[3] = {"AGENT", "COMMANDER", "HERO"};
            std::string head = card_.kind == 6 ? std::string("LEADER \xC2\xB7 ") + kType[kLeaders[card_.card].type] : "FACTION ABILITY";
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, shade(base, 0.7f), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, head, theme::text, a);
            if (card_.player >= 0) {
                drawFactionIcon(c, g.players[card_.player].faction, r.x + 8, r.y + 7, 20, a);
                drawFactionIcon(c, g.players[card_.player].faction, r.x + r.w - 28, r.y + 7, 20, a);
            }
            auto lines = wrapText(FONT_MDB, card_.title, r.w - 16);
            int y = r.y + 36;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_MDB, r.x, y, r.w, Align::Center, lines[k], theme::text, a);
                y += 19;
            }
            std::string body = card_.kind == 6 ? std::string(leaderText(card_.card)) : card_.text;
            auto text = wrapText(FONT_XS, body, r.w - 16);
            for (size_t k = 0; k < text.size() && k < 3; ++k) {
                drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, text[k], rgb(225, 215, 235), a);
                y += 12;
            }
        } else if (card_.kind == 8) {
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, shade(base, 0.7f), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, kAgendas[card_.card].law ? "AGENDA \xC2\xB7 LAW" : "AGENDA \xC2\xB7 DIRECTIVE",
                            theme::text, a);
            headerIcons(c, r, "agenda", a);
            auto lines = wrapText(FONT_MDB, card_.title, r.w - 16);
            int y = r.y + 36;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_MDB, r.x, y, r.w, Align::Center, lines[k], theme::text, a);
                y += 19;
            }
            auto text = wrapText(FONT_XS, agendaText(card_.card, false), r.w - 16);
            for (size_t k = 0; k < text.size() && k < 3; ++k) {
                drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, text[k], rgb(205, 215, 240), a);
                y += 12;
            }
        } else if (card_.kind == 9) {
            static const char* kHead[6] = {"OFFER", "DEAL MADE", "DECLINED", "PROMISE BROKEN", "NO DEAL", "COUNTER-OFFER"};
            int type = std::max(0, std::min(5, card_.card));
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, shade(base, 0.7f), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, kHead[type], theme::text, a);
            headerIcons(c, r, "promissory", a, 18);
            if (talk) {  // 0.57.0: the speaker on the comms - portrait, words typing out, then what happened
                const int spk = card_.speaker;
                const Rect pw{r.x + 6, r.y + 34, 60, 60};
                codec::window(c, g.players[spk].faction, pw, pcolor(g, spk), now, uint32_t(age), a, 700);
                if (!card_.laid) {
                    card_.laid = true;
                    card_.sp = speech::layoutFit(g.players[spk].faction, card_.line, r.w - 80, 84, pcolor(g, spk), rgb(226, 230, 244), card_.seed);
                }
                // (0.58.4: typed at 80 a second - the banner now lasts until it can be read)
                const int shownChars = age > 250 ? speech::typed(card_.sp, uint32_t(age - 250) * 80U / 55U) : 0;
                speech::draw(c, card_.sp, r.x + 72, r.y + 34, shownChars, now, a);
                int y = r.y + 34 + std::max(62, card_.sp.height + 4);
                for (const std::string& l : wrapText(FONT_XS, card_.text, r.w - 14)) {
                    if (y + 12 > r.y + r.h - 4) break;
                    drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, l, rgb(190, 198, 216), a);
                    y += 12;
                }
                return;
            }
            // the two players, joined by a line in the colour of what happened
            const float ly = float(r.y + 52), x0 = float(r.x + 46), x1 = float(r.x + r.w - 46);
            c.line(x0, ly, x1, ly, shade(base, 1.4f), 6.f, a / 5);
            c.line(x0, ly, x1, ly, shade(base, 1.4f), 2.5f, a);
            for (int k = 0; k < 2; ++k) {
                int p = k == 0 ? card_.player : card_.other;
                if (p < 0 || p >= g.numPlayers) continue;
                float x = k == 0 ? x0 : x1;
                c.fillCircle(x, ly, 16.f, rgb(10, 14, 26), a);
                c.ring(x, ly, 16.f, 2.5f, pcolor(g, p), a);
                drawFactionIcon(c, g.players[p].faction, int(x) - 12, int(ly) - 12, 24, a);
            }
            auto text = wrapText(FONT_XS, card_.text, r.w - 14);
            int y = r.y + 74;
            for (size_t k = 0; k < text.size() && k < 5; ++k) {
                std::string l = text[k];
                if (k == 4 && text.size() > 5) l = fitText(FONT_XS, l + " ...", r.w - 14);
                drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, l, rgb(220, 226, 238), a);
                y += 12;
            }
        } else if (card_.kind == 3) {
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, rgb(150, 40, 35), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, "ACTION CARD", theme::text, a);
            headerIcons(c, r, "action_card", a);
            auto lines = wrapText(FONT_MDB, card_.title, r.w - 16);
            int y = r.y + 36;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_MDB, r.x, y, r.w, Align::Center, lines[k], theme::text, a);
                y += 19;
            }
            auto text = wrapText(FONT_XS, actionCardText(card_.card), r.w - 16);
            for (size_t k = 0; k < text.size() && k < 3; ++k) {
                drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, text[k], rgb(230, 200, 195), a);
                y += 12;
            }
        } else {
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 26, 8, shade(base, 0.7f), a);
            drawTextAligned(c, FONT_SMB, r.x, r.y + 9, r.w, Align::Center, "TECHNOLOGY", theme::text, a);
            if (card_.card >= 0 && card_.card < kTechCount && !kTechs[card_.card].unitUpgrade && kTechs[card_.card].color != TechColor::None) {
                drawSkipIcon(c, kTechs[card_.card].color, r.x + 8, r.y + 7, 20, a);
                drawSkipIcon(c, kTechs[card_.card].color, r.x + r.w - 28, r.y + 7, 20, a);
            }
            auto lines = wrapText(FONT_MDB, card_.title, r.w - 16);
            int y = r.y + 42;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_MDB, r.x, y, r.w, Align::Center, lines[k], theme::text, a);
                y += 19;
            }
        }
        uint32_t who = card_.player >= 0 ? pcolor(g, card_.player) : theme::text;
        auto sub = wrapText(FONT_SM, card_.sub, r.w - 16);
        int y = r.y + r.h - 8 - int(std::min<size_t>(2, sub.size())) * 16;
        for (size_t k = 0; k < sub.size() && k < 2; ++k) {
            drawTextAligned(c, FONT_SM, r.x, y, r.w, Align::Center, sub[k], who, a);
            y += 16;
        }
    }

    // A computer player turns on someone (owner, RULES-CHECK 0.12.0 #4): who was offended, by whom, and what it
    // will do; and (0.57.0) a promise broken to or by a person. Waits for A like the reveal. 0.57.0: the betrayal
    // codec - the red channel, both portraits, the betrayer's words, then the victim's answer.
    void drawBetrayal(Canvas& c, const Game& g, uint32_t now, bool holding) {
        const int age = int(now - card_.start);
        const int a = holding ? std::min(255, 60 + age * 195 / 300) : fadeAlpha(now, card_.start, card_.until, 300, 400);
        if (a <= 0) return;
        const int bot = card_.player, t = card_.other;
        if (bot < 0 || bot >= g.numPlayers || t < 0 || t >= g.numPlayers) return;
        const bool broken = card_.kind == 21;
        const bool kingslay = !broken && card_.card == int(Betrayal::Kingslay);  // 0.19.0: Mecatol Rex held too long
        codec::backdrop(c, now, true, kTopH, kHintY);
        if (age < 600) c.fillRect(0, kTopH, 640, kHintY - kTopH, rgb(255, 60, 40), (600 - age) * 70 / 600);  // a flash as it lands
        // the title, glitching in the red channel
        const std::string title = kingslay ? "KINGSLAY!" : broken ? "PROMISE BROKEN" : "BETRAYAL!";
        const int jit = (now / 110U) % 7U == 0 ? 3 : 1;
        drawTextAligned(c, FONT_XL, -jit, 28, 640, Align::Center, title, rgb(60, 230, 255), a * 140 / 255);
        drawTextAligned(c, FONT_XL, jit, 28, 640, Align::Center, title, rgb(255, 60, 200), a * 140 / 255);
        drawTextAligned(c, FONT_XL, 0, 28, 640, Align::Center, title, rgb(255, 225, 225), a);
        const std::string sub = kingslay ? playerName(g, t) + " has sat on the throne too long!"
                                : broken ? playerName(g, bot) + " breaks a promise to " + playerName(g, t)
                                         : playerName(g, bot) + " was offended!";
        drawTextAligned(c, FONT_SMB, 0, 68, 640, Align::Center, fitText(FONT_SMB, sub, 600), rgb(255, 200, 150), a);
        // the two windows
        const Rect lw{36, 90, 116, 116}, rw{488, 90, 116, 116};
        codec::window(c, g.players[bot].faction, lw, pcolor(g, bot), now, uint32_t(age), a, 0, true);
        codec::window(c, g.players[t].faction, rw, pcolor(g, t), now, uint32_t(std::max(0, age - 150)), a, 1500, true);
        auto who = [&](int q) { return playerName(g, q) + (session_->isHumanSeat(q) ? " (you)" : ""); };
        codec::plate(c, g.players[bot].faction, Rect{36, 210, 116, 28}, who(bot), kingslay ? "Marches on Mecatol" : "Betrayer", pcolor(g, bot), a);
        codec::plate(c, g.players[t].faction, Rect{488, 210, 116, 28}, who(t), kingslay ? "On the throne" : "Betrayed", pcolor(g, t), a);
        // the broken channel and what it is about
        codec::signal(c, 320, 92, "", rgb(255, 90, 80), now, false, true, a);
        {
            std::string what = broken ? card_.text : kingslay ? std::string("Its objective: take Mecatol Rex") : "Its scheme: " + card_.title;
            if (broken) {  // "X breaks a promise to Y (the promise): how." -> the promise and how
                const size_t o = what.find('(');
                if (o != std::string::npos) what = "Promise broken: " + what.substr(o + 1);
                const size_t cpar = what.find(')');
                if (cpar != std::string::npos) what.erase(cpar, 1);
            }
            auto wrap3 = [](const std::string& x) {
                std::vector<std::string> v = wrapText(FONT_XS, x, 300);
                if (v.size() > 3) {
                    v.resize(3);
                    v[2] = fitText(FONT_XS, v[2] + " ...", 300);
                }
                return v;
            };
            int y = 156;
            for (const std::string& l : wrap3(what)) {
                drawTextAligned(c, FONT_XS, 170, y, 300, Align::Center, l, rgb(255, 170, 150), a);
                y += 12;
            }
            if (!broken)
                for (const std::string& l : wrap3(card_.text)) {
                    drawTextAligned(c, FONT_XS, 170, y, 300, Align::Center, l, rgb(220, 210, 220), a);
                    y += 12;
                }
        }
        // the exchange: the betrayer speaks, then the victim answers
        const Rect b1{36, 246, 400, 76}, b2{204, 328, 400, 76};
        if (!card_.laid) {
            card_.laid = true;
            const Rect a1 = codec::speechArea(b1), a2 = codec::speechArea(b2);
            if (!card_.line.empty())
                card_.sp = speech::layoutFit(g.players[bot].faction, card_.line, a1.w, a1.h, pcolor(g, bot), rgb(240, 232, 236), card_.seed);
            if (!card_.reply.empty())
                card_.sp2 = speech::layoutFit(g.players[t].faction, card_.reply, a2.w, a2.h, pcolor(g, t), rgb(240, 232, 236), card_.seed >> 3);
        }
        const int start1 = 600, typed1 = age > start1 ? speech::typed(card_.sp, uint32_t(age - start1)) : 0;
        if (!card_.line.empty()) codec::speechBox(c, b1, upperName(g, bot), pcolor(g, bot), card_.sp, typed1, now, true, a);
        const int start2 = start1 + card_.sp.chars * 1000 / 55 + 500;
        if (!card_.reply.empty() && age > start2 - 300) {
            const int ra = std::min(a, (age - (start2 - 300)) * 255 / 300);
            codec::speechBox(c, b2, upperName(g, t), pcolor(g, t), card_.sp2, age > start2 ? speech::typed(card_.sp2, uint32_t(age - start2)) : 0,
                             now, true, ra);
        }
        if (holding) {
            int bw = 130, bx = (640 - bw) / 2, by = 410;
            const int pulse = 160 + int(95 * (0.5f + 0.5f * std::sin(float(age) / 180.f)));
            c.fillRoundRect(bx, by, bw, 24, 8, rgb(110, 30, 30), a);
            c.frameRoundRect(bx, by, bw, 24, 8, rgb(255, 130, 110), 1, std::min(a, pulse));
            continueKey(c, bx, by, bw, a);
        }
    }

    // 0.57.0: a timed betrayal codec (Cards: timed) stays up until both lines have typed out and can be read
    void stretchForExchange() {
        const uint32_t need = 600U + uint32_t(card_.line.size() + 40U) * 1000U / 55U + 500U + uint32_t(card_.reply.size()) * 1000U / 55U + 1800U;
        if (int32_t(card_.start + need - card_.until) > 0) card_.until = card_.start + need;
    }

    static std::string upperName(const Game& g, int p) {
        std::string s = playerName(g, p);
        for (char& ch : s) ch = char(std::toupper((unsigned char)ch));
        return s;
    }

    // End of round 3: "Our Enemies' Ambitions have been Revealed!" - every computer player's archetype.
    // Mecatol Rex planetfall (0.24.0, owner's picture): the night side of Mecatol Rex,
    // its capital a web of golden light; a band of light falls onto Mecatol City,
    // and once it lands: "The Council Has Been Summoned!" - waits for A.
    // (widescreen, 0.46.0: as wide as the screen, Mecatol City in the middle)
    static const Canvas& planetfallScene(float scale, int w) {
        static std::unique_ptr<Canvas> bg;
        if (bg && bg->scale() == scale && bg->width() == w) return *bg;
        bg = std::make_unique<Canvas>(w, 480, scale);  // (0.27.0: at the screen's real size)
        Canvas& b = *bg;
        Canvas sky(w, 480, scale);
        sky.verticalGradient(0, 0, w, 480, rgb(2, 3, 12), rgb(10, 22, 58));
        const float fw = float(w), mid = fw / 2.f;
        uint32_t s = 12345u;
        auto rnd = [&](float lo, float hi) {
            s = s * 1664525u + 1013904223u;
            return lo + (hi - lo) * float((s >> 8) % 10000) / 10000.f;
        };
        for (int i = 0; i < 420; ++i) {
            const int v = int(rnd(60, 230));
            sky.pixel(int(rnd(0, fw)), int(rnd(0, 330)), rgb(v, v, std::min(255, v + 30)), int(rnd(90, 255)));
        }
        // the planet: a huge disc, its horizon across the middle
        const float pcx = mid, pcy = 1305.f, pr = 1000.f;
        // the atmosphere's glow over the horizon, kept apart so nothing on the ground spills into it
        for (int k = 64; k >= 4; k -= 3)  // a soft glow, brighter toward the ground
            sky.fillCircle(pcx, pcy, pr + float(k), mix(rgb(30, 90, 230), rgb(140, 200, 255), 255 - k * 4), 10 + (64 - k) / 4);
        sky.fillCircle(pcx, pcy, pr + 3.f, rgb(190, 225, 255), 190);
        sky.fillCircle(pcx, pcy, pr + 1.f, rgb(235, 245, 255), 230);
        b = sky;
        b.fillCircle(pcx, pcy, pr, rgb(30, 24, 18));
        for (int i = 0; i < 160; ++i) {  // dark and pale land
            const float x = rnd(0, fw), y = rnd(300, 480);
            b.fillCircle(x, y, rnd(8, 40), i % 2 ? rgb(44, 34, 24) : rgb(16, 12, 10), 90);
        }
        // Mecatol City and the web of light around it (seen at a slant)
        const float ccx = mid, ccy = 405.f;
        for (int k = 1; k <= 7; ++k) {  // ring roads
            const float rx = 34.f * float(k) * (1.f + 0.12f * float(k)), ry = rx * 0.3f;
            float lx = 0, ly = 0;
            for (int i = 0; i <= 72; ++i) {
                const float a = 2 * 3.14159265f * float(i) / 72.f;
                const float x = ccx + std::cos(a) * rx, y = ccy + std::sin(a) * ry;
                if (i) b.line(lx, ly, x, y, rgb(255, 196, 110), 1.f, std::max(40, 150 - k * 14));
                lx = x;
                ly = y;
            }
        }
        for (int i = 0; i < 20; ++i) {  // the great avenues out from the centre
            const float a = 2 * 3.14159265f * float(i) / 20.f + 0.05f;
            b.line(ccx, ccy, ccx + std::cos(a) * 640.f, ccy + std::sin(a) * 200.f, rgb(255, 200, 120), 1.f, 120);
        }
        for (int i = 0; i < 14; ++i) {  // winding roads further out
            float x = rnd(0, fw), y = rnd(320, 480);
            for (int k = 0; k < 6; ++k) {
                const float nx = x + rnd(-40, 40), ny = y + rnd(-10, 10);
                b.line(x, y, nx, ny, rgb(230, 170, 90), 1.f, 90);
                x = nx;
                y = ny;
            }
        }
        for (int i = 0; i < 2600; ++i) {  // city lights, thickest at the centre
            const float q = rnd(0, 1), a = rnd(0, 2 * 3.14159265f), d = q * std::sqrt(q) * 330.f;  // (no powf: Miyoo glibc)
            const float x = ccx + std::cos(a) * d * 1.6f, y = ccy + std::sin(a) * d * 0.42f;
            b.pixel(int(x), int(y), i % 5 ? rgb(255, 200, 120) : rgb(255, 245, 210), int(rnd(90, 255)));
        }
        for (int i = 0; i < 9; ++i) {  // lit district hubs
            const float a = rnd(0, 2 * 3.14159265f), d = rnd(80, 300);
            const float x = ccx + std::cos(a) * d * 1.5f, y = ccy + std::sin(a) * d * 0.35f;
            b.fillCircle(x, y, 3.f, rgb(255, 240, 200), 220);
            b.fillCircle(x, y, 9.f, rgb(255, 210, 140), 50);
        }
        b.fillCircle(ccx, ccy, 26.f, rgb(255, 220, 150), 60);
        b.fillCircle(ccx, ccy, 9.f, rgb(255, 250, 230), 230);
        // the sky above the horizon again (the roads stop at the planet's edge), then the bright rim
        uint32_t* px = b.pixels();
        const uint32_t* sp = sky.pixels();
        const int W = b.pw(), H = b.ph();
        for (int x = 0; x < W; ++x) {
            const float dx = (float(x) + 0.5f) / scale - pcx;
            const int top = int((pcy - std::sqrt(std::max(0.f, pr * pr - dx * dx))) * scale);
            for (int y = 0; y < std::min(H, top); ++y) px[size_t(y) * size_t(W) + size_t(x)] = sp[size_t(y) * size_t(W) + size_t(x)];
        }
        return *bg;
    }

    void drawPlanetfall(Canvas& c, const Game& g, uint32_t now, bool holding) {
        const int t = int(now - card_.start);
        const int a = holding ? std::min(255, t * 255 / 500) : fadeAlpha(now, card_.start, card_.until, 500, 500);
        if (a <= 0) return;
        const Canvas& bg = planetfallScene(c.scale(), c.fullWidth());
        const size_t n = std::min(size_t(bg.pw()) * size_t(bg.ph()), size_t(c.pw()) * size_t(c.ph()));
        if (a >= 255) std::copy(bg.pixels(), bg.pixels() + n, c.pixels());
        else {
            uint32_t* px = c.pixels();
            const uint32_t* bp = bg.pixels();
            for (size_t i = 0; i < n; ++i) px[i] = mix(px[i], bp[i], a);
        }
        CentredView view(c);  // (widescreen: the landing and the banner over the city in the middle)
        const float ccx = 320.f, ccy = 405.f;
        const int fallStart = 600, impact = 2300;
        // the band of light: a ship coming down
        if (t >= fallStart && t < impact + 80) {
            const float k = std::min(1.f, float(t - fallStart) / float(impact - fallStart));
            const float e = k * k;  // speeding up
            auto at = [&](float q, float& x, float& y) {
                x = 520.f + (ccx - 520.f) * q + std::sin(q * 3.14159265f) * 30.f;
                y = -30.f + (ccy + 30.f) * q;
            };
            float hx, hy;
            at(e, hx, hy);
            for (int i = 1; i <= 14; ++i) {  // the trail
                const float q0 = std::max(0.f, e - 0.03f * float(i)), q1 = std::max(0.f, e - 0.03f * float(i - 1));
                float x0, y0, x1, y1;
                at(q0, x0, y0);
                at(q1, x1, y1);
                const int ta = 230 - i * 15;
                c.line(x0, y0, x1, y1, rgb(120, 190, 255), 9.f - float(i) * 0.5f, ta / 4);
                c.line(x0, y0, x1, y1, rgb(230, 245, 255), std::max(1.f, 4.f - float(i) * 0.25f), ta);
            }
            c.fillCircle(hx, hy, 16.f, rgb(140, 200, 255), 70);
            c.fillCircle(hx, hy, 6.f, rgb(255, 255, 255), 255);
        }
        // the landing: a flash, a shock ring across the city, the lights flaring out along the avenues
        if (t >= impact) {
            const float k = float(t - impact);
            if (k < 600.f) c.fillCircle(ccx, ccy, 10.f + k * 0.3f, rgb(255, 255, 240), int(255.f * (1.f - k / 600.f)));
            for (int r = 0; r < 2; ++r) {
                const float q = (k - float(r) * 250.f) / 1500.f;
                if (q <= 0.f || q >= 1.f) continue;
                const float rx = 30.f + 420.f * q, ry = rx * 0.3f;
                float lx = 0, ly = 0;
                for (int i = 0; i <= 64; ++i) {
                    const float ang = 2 * 3.14159265f * float(i) / 64.f;
                    const float x = ccx + std::cos(ang) * rx, y = ccy + std::sin(ang) * ry;
                    if (i) c.line(lx, ly, x, y, rgb(255, 230, 170), 2.5f, int(220.f * (1.f - q)));
                    lx = x;
                    ly = y;
                }
            }
            for (int i = 0; i < 20; ++i) {  // light running out along the avenues
                const float ang = 2 * 3.14159265f * float(i) / 20.f + 0.05f;
                const float q = std::fmod(k / 1400.f + float(i % 4) * 0.25f, 1.f);
                c.fillCircle(ccx + std::cos(ang) * 640.f * q, ccy + std::sin(ang) * 200.f * q, 2.5f, rgb(255, 245, 210), int(230.f * (1.f - q)));
            }
            const float glow = 0.6f + 0.4f * std::sin(k / 300.f);
            c.fillCircle(ccx, ccy, 30.f, rgb(255, 220, 150), int(70 * glow));
            c.fillCircle(ccx, ccy, 11.f, rgb(255, 255, 240), 240);
        }
        // the banner
        if (t >= impact + 250) {
            const int ba = std::min(255, (t - impact - 250) * 255 / 400) * a / 255;
            const Rect r{40, 46, 560, 92};
            c.fillRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 12, rgb(0, 0, 0), ba * 150 / 255);
            c.fillRoundRect(r.x, r.y, r.w, r.h, 10, rgb(20, 16, 10), ba * 230 / 255);
            c.frameRoundRect(r.x, r.y, r.w, r.h, 10, theme::gold, 2, ba);
            drawTextAligned(c, FONT_LG, r.x, r.y + 12, r.w, Align::Center, "The Council Has Been Summoned!", rgb(255, 214, 120), ba);
            const int p = card_.player;
            std::string who = p >= 0 && p < g.numPlayers ? (p == session_->human() ? std::string("You remove") : playerName(g, p) + " removes") : "";
            if (!who.empty()) {
                drawFactionIcon(c, g.players[size_t(p)].faction, r.x + 16, r.y + 46, 30, ba);
                drawTextAligned(c, FONT_SM, r.x + 40, r.y + 44, r.w - 50, Align::Center,
                                who + " the custodians token from Mecatol Rex: 1 victory point.", p >= 0 ? pcolor(g, p) : theme::text, ba);
                drawTextAligned(c, FONT_XS, r.x + 40, r.y + 64, r.w - 50, Align::Center, "From now on, every round ends with an agenda phase.",
                                theme::dim, ba);
            }
        }
        if (holding && t >= impact + 600) {
            const int pulse = 170 + int(85 * (0.5f + 0.5f * std::sin(float(t) / 260.f)));
            const std::string ct = keyText("A: Continue");
            const int bw = textWidth(FONT_SMB, ct) + 30, bx = 320 - bw / 2, by = 150;
            c.fillRoundRect(bx, by, bw, 24, 8, rgb(30, 24, 10), 230);
            c.frameRoundRect(bx, by, bw, 24, 8, theme::gold, 1, pulse);
            drawTextAligned(c, FONT_SMB, bx, by + 4, bw, Align::Center, ct, theme::text);
        }
    }

    void drawReveal(Canvas& c, const Game& g, uint32_t now, bool holding) {
        const int age = int(now - card_.start);
        const int a = holding ? std::min(255, 60 + age * 195 / 300) : fadeAlpha(now, card_.start, card_.until, 300, 400);
        if (a <= 0) return;
        std::vector<int> bots;
        for (int q = 0; q < g.numPlayers; ++q)
            if (isBot(g, q)) bots.push_back(q);
        const int rowH = bots.size() > 6 ? 40 : 46;
        const int h = 62 + int(bots.size()) * rowH + (holding ? 40 : 14);
        Rect r{12, kTopH + std::max(4, (kHintY - kTopH - h) / 2), 616, h};
        c.fillRect(0, kTopH, 640, kHintY - kTopH, rgb(0, 0, 0), a * 150 / 255);
        c.fillRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 14, rgb(0, 0, 0), a * 170 / 255);
        c.fillRoundRect(r.x, r.y, r.w, r.h, 12, rgb(28, 10, 14), a);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 12, rgb(220, 70, 60), 3, a);
        if (age < 700) {  // a red sweep while it appears
            int sx = r.x + age * (r.w + 160) / 700 - 80;
            c.fillRect(std::max(r.x, sx - 40), r.y + 3, 80, 46, rgb(255, 120, 90), a * 60 / 255);
        }
        drawTextAligned(c, FONT_LG, r.x, r.y + 12, r.w, Align::Center, "Our Enemies' Ambitions have been Revealed!", rgb(255, 214, 120), a);
        drawTextAligned(c, FONT_XS, r.x, r.y + 40, r.w, Align::Center,
                        keyText("Their deeper plans and grudges stay hidden. MENU > Negotiations shows each one's attitude to you."), theme::dim, a);
        int y = r.y + 58;
        for (int q : bots) {
            const Player& pl = g.players[q];
            const ArchDef& d = kArchetypes[pl.pers.arch];
            c.fillCircle(float(r.x + 30), float(y + rowH / 2), 17.f, rgb(10, 14, 26), a);
            c.ring(float(r.x + 30), float(y + rowH / 2), 17.f, 2.5f, pcolor(g, q), a);
            drawFactionIcon(c, pl.faction, r.x + 17, y + rowH / 2 - 13, 26, a);
            drawText(c, FONT_MDB, r.x + 56, y + 5, playerName(g, q) + (q == session_->seat() ? " (your seat)" : ""), pcolor(g, q), a);
            drawText(c, FONT_XS, r.x + 56, y + 24, fitText(FONT_XS, kFactions[pl.faction].name, 200), theme::faint, a);
            drawText(c, FONT_LG, r.x + 270, y + 4, d.name, theme::text, a);
            drawText(c, FONT_XS, r.x + 270, y + 28, fitText(FONT_XS, d.blurb, r.w - 280), rgb(240, 200, 190), a);
            y += rowH;
        }
        if (holding) {
            int pulse = 170 + int(85 * (0.5f + 0.5f * std::sin(float(age) / 260.f)));
            int bw = 130, bx = r.x + (r.w - bw) / 2, by = r.y + r.h - 32;
            c.fillRoundRect(bx, by, bw, 24, 8, rgb(110, 30, 30), a);
            c.frameRoundRect(bx, by, bw, 24, 8, rgb(255, 130, 110), 1, std::min(a, pulse));
            continueKey(c, bx, by, bw, a);
        }
    }

    static uint32_t dealColor(int type) {
        switch (type) {
            case kDealMade: return rgb(60, 165, 95);
            case kDealBroken: return rgb(200, 60, 60);
            case kDealDeclined:
            case kDealFailed: return rgb(105, 110, 128);
            default: return rgb(50, 140, 195);
        }
    }
    // A drawn exploration / relic card, large and complete, until A is pressed.
    static uint32_t agendaColor(int a) { return a >= 0 && kAgendas[a].law ? rgb(70, 120, 220) : rgb(120, 120, 165); }

    // An exploration card first shows its deck's back, which wipes away to reveal it.
    // (0.33.1, owner) An action card that is played does the same with the action card back.
    static constexpr int kExploreReveal = 650;
    static bool revealed(int kind) { return kind == 4 || kind == 3; }
    bool drawExploreReveal(Canvas& c, uint32_t now) {
        const int age = int(now - card_.start);
        if (!revealed(card_.kind) || card_.card < 0 || age >= kExploreReveal) return false;
        const bool action = card_.kind == 3;
        const int w = 150, h = action ? 238 : 225;
        const Rect r{(mapW() - w) / 2, kTopH + (360 - h) / 2 + 10, w, h};
        c.fillRect(0, kTopH, mapW(), 480 - kTopH - 28, rgb(0, 0, 0), std::min(110, age * 110 / 200));
        const int deck = action ? 0 : int(kExploreCards[card_.card].type) & 3;
        const int a = std::min(255, 60 + age * 195 / 180);
        // after a moment the back narrows to a line from both sides (a flip)
        const int flipAt = 380;
        int cw = w;
        if (age > flipAt) cw = std::max(0, w - (age - flipAt) * w / (kExploreReveal - flipAt));
        if (cw <= 0) return true;
        const Rect old = c.clip();
        c.setClip(Rect{r.x + (w - cw) / 2, r.y - 8, cw, h + 16});
        if (action) cards::actionBack(c, r, a);
        else cards::exploreBack(c, deck, r, a);
        c.setClip(old);
        return true;
    }

    // The key and "Continue" inside a held card's button - the key named for the controls in use
    // (0.29.0: it always said "A", also with keyboard & mouse); a click on it presses A.
    static void continueKey(Canvas& c, int bx, int by, int bw, int a) {
        const std::string key = keyLabel("A");
        const int kw = std::max(18, textWidth(FONT_SMB, key) + 8);
        const int tw = textWidth(FONT_SMB, "Continue");
        const int x0 = bx + (bw - (kw + 6 + tw)) / 2;
        c.fillRoundRect(x0, by + 3, kw, 18, 9, theme::text, a);
        drawTextAligned(c, FONT_SMB, x0, by + 4, kw, Align::Center, key, rgb(20, 20, 30), a);
        drawText(c, FONT_SMB, x0 + kw + 6, by + 4, "Continue", theme::text, a);
        hits::addButton(Rect{bx, by, bw, 24}, Btn::A);
    }
    void drawHeldCard(Canvas& c, const Game& g, uint32_t now) {
        static const uint32_t kDeckCol[4] = {rgb(80, 130, 220), rgb(210, 80, 70), rgb(80, 180, 100), rgb(150, 130, 190)};
        // kinds: 3 action card, 4 exploration, 5 relic gained, 8 agenda, 12 secret objective
        // scored, 13 relic used, 14 agenda ruling, 15 component action (0.16.0: owner - cards
        // shown large when used, until A)
        const int kind = card_.kind;
        const bool relic = kind == 5 || kind == 13;
        const bool agenda = kind == 8;
        const bool ruling = kind == 14, action = kind == 3 || kind == 17, secret = kind == 12 || kind == 18, comp = kind == 15,
                   leader = kind == 16;
        if (drawExploreReveal(c, now)) return;
        const uint32_t base = agenda || ruling ? agendaColor(card_.card)
                              : relic          ? rgb(200, 160, 60)
                              : action         ? rgb(210, 70, 60)
                              : secret         ? (kObjectives[card_.card].kind == 3   ? rgb(200, 60, 80)
                                                  : kObjectives[card_.card].kind == 2 ? rgb(70, 120, 220)
                                                                                      : rgb(215, 165, 55))
                              : comp           ? rgb(70, 150, 210)
                              : leader         ? (kLeaders[card_.card].type == 0   ? rgb(90, 170, 120)
                                                  : kLeaders[card_.card].type == 1 ? rgb(90, 140, 210)
                                                                                   : rgb(200, 150, 60))
                                               : kDeckCol[int(kExploreCards[card_.card].type)];
        const int age = int(now - card_.start) - (revealed(kind) ? kExploreReveal : 0);
        const int a = std::min(255, 60 + age * 195 / 200);
        const int w = agenda || ruling || comp ? 380 : 360;
        std::vector<std::string> textLines;
        if (ruling || comp) {
            textLines = wrapText(FONT_MD, card_.text, w - 36);
        } else if (action) {
            textLines = wrapText(FONT_MD, actionCardText(card_.card), w - 36);
        } else if (secret) {
            textLines = wrapText(FONT_MD, objectiveSummary(card_.card), w - 36);
        } else if (leader) {
            textLines = wrapText(FONT_MD, leaderText(card_.card), w - 36);
        } else if (agenda) {
            const bool fa = kAgendas[card_.card].elect == Elect::ForAgainst;
            textLines = wrapText(FONT_MD, std::string(fa ? "For: " : "") + agendaText(card_.card, false), w - 36);
            if (fa) {
                textLines.push_back("");
                for (const std::string& l : wrapText(FONT_MD, std::string("Against: ") + agendaText(card_.card, true), w - 36))
                    textLines.push_back(l);
            }
        } else {
            textLines = wrapText(FONT_MD, relic ? relicText(card_.card) : exploreText(card_.card), w - 36);
        }
        auto titleLines = wrapText(FONT_LG, card_.title, w - 32);
        auto subLines = wrapText(FONT_SMB, card_.sub, w - 32);
        std::string kindLine;
        if (ruling) {
            const bool rejected = card_.text.compare(0, 13, "Against wins.") == 0 || card_.text.find("Deadly Plot") != std::string::npos;
            kindLine = !kAgendas[card_.card].law ? "Directive - resolved now."
                       : rejected                ? "The law does not pass."
                                                 : keyText("Law - it stays in play (MENU > Laws).");
        } else if (action) {
            kindLine = kind == 17 ? "Action card - new in your hand (MENU > Your cards)." : "Action card - discarded once it is played.";
        } else if (secret) {
            const int pts = kObjectives[card_.card].points;
            const int ok = kObjectives[card_.card].kind;
            kindLine = std::string(ok == 3 ? "Secret objective" : ok == 2 ? "Public objective, stage II" : "Public objective, stage I") +
                       " - " + std::to_string(pts) + (pts == 1 ? " victory point" : " victory points") +
                       (kind == 18 ? ", newly revealed." : ".");
        } else if (comp) {
            kindLine = "Component action - it takes the player's action for this turn.";
        } else if (leader) {
            const int lt = kLeaders[card_.card].type;
            kindLine = lt == 0 ? "Agent - exhausted until the status phase readies it."
                       : lt == 1 ? "Commander - always on once unlocked."
                                 : "Hero - purged once it has been used.";
        } else if (agenda) {
            kindLine = kAgendas[card_.card].law ? "Law - if it passes, it stays in play." : "Directive - a one-time effect.";
        } else if (!relic) {
            switch (kExploreCards[card_.card].kind) {
                case ExploreKind::Attach: kindLine = "Attachment - it stays on the planet."; break;
                case ExploreKind::Fragment: kindLine = "Relic fragment - kept in the play area."; break;
                case ExploreKind::Token: kindLine = "Token - placed on the board."; break;
                default: break;
            }
        } else {
            kindLine = keyText("Relic - kept in the play area (MENU > Your cards > Relics).");
        }
        auto kindLines = wrapText(FONT_XS, kindLine, w - 32);
        int h = 12 + 30 + 10 + int(titleLines.size()) * 26 + 8 + int(textLines.size()) * (FONT_MD.lineHeight + 2) + 10 +
                int(kindLines.size()) * 14 + 8 + int(subLines.size()) * 17 + 34;
        Rect r{(mapW() - w) / 2, kTopH + std::max(8, (360 - h) / 2), w, h};
        c.fillRect(0, kTopH, mapW(), 480 - kTopH - 28, rgb(0, 0, 0), a * 110 / 255);
        c.fillRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 14, rgb(0, 0, 0), a * 160 / 255);
        c.fillRoundRect(r.x, r.y, r.w, r.h, 12, shade(base, 0.25f), a);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 12, shade(base, 1.3f), 3, a);
        std::string head = agenda   ? std::string("Agenda \xC2\xB7 ") + (kAgendas[card_.card].law ? "law" : "directive")
                           : ruling ? "Agenda ruling"
                           : relic  ? "RELIC"
                           : action ? "Action card"
                           : secret ? (kObjectives[card_.card].kind == 3 ? "Secret objective" : "Public objective")
                           : comp   ? "Component action"
                           : leader ? (kLeaders[card_.card].type == 0 ? "Agent" : kLeaders[card_.card].type == 1 ? "Commander" : "Hero")
                                    : std::string(exploreTypeName(kExploreCards[card_.card].type)) + " exploration";
        for (char& ch : head) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        c.fillRoundRect(r.x + 6, r.y + 6, r.w - 12, 30, 9, shade(base, 0.7f), a);
        drawTextAligned(c, FONT_MDB, r.x, r.y + 12, r.w, Align::Center, head, theme::text, a);
        if (const char* ic = agenda || ruling ? "agenda" : action ? "action_card" : secret ? "secret" : relic ? "relic"
                             : leader                                                       ? nullptr
                                                                                  : cardIcon(card_.kind, card_.card)) {
            icons::draw(c, ic, r.x + 12, r.y + 9, 24, a);
            icons::draw(c, ic, r.x + r.w - 36, r.y + 9, 24, a);
        }
        int y = r.y + 12 + 30 + 10;
        for (const std::string& l : titleLines) {
            drawTextAligned(c, FONT_LG, r.x, y, r.w, Align::Center, l, theme::text, a);
            y += 26;
        }
        y += 8;
        for (const std::string& l : textLines) {
            drawTextAligned(c, FONT_MD, r.x, y, r.w, Align::Center, l, rgb(232, 226, 210), a);
            y += FONT_MD.lineHeight + 2;
        }
        y += 10;
        for (const std::string& l : kindLines) {
            drawTextAligned(c, FONT_XS, r.x, y, r.w, Align::Center, l, shade(base, 1.5f), a);
            y += 14;
        }
        y += 8;
        uint32_t who = card_.player >= 0 ? pcolor(g, card_.player) : theme::text;
        for (const std::string& l : subLines) {
            drawTextAligned(c, FONT_SMB, r.x, y, r.w, Align::Center, l, who, a);
            y += 17;
        }
        // "A  Continue" button, gently pulsing
        int pulse = 170 + int(85 * (0.5f + 0.5f * std::sin(float(age) / 260.f)));
        int bw = 130, bx = r.x + (r.w - bw) / 2, by = r.y + r.h - 32;
        c.fillRoundRect(bx, by, bw, 24, 8, shade(base, 0.55f), a);
        c.frameRoundRect(bx, by, bw, 24, 8, shade(base, 1.4f), 1, std::min(a, pulse));
        continueKey(c, bx, by, bw, a);
    }

    void drawToast(Canvas& c, uint32_t now) {
        if (toastUntil_ == 0 || int32_t(now - toastUntil_) >= 0) return;
        int a = fadeAlpha(now, toastStart_, toastUntil_);
        auto lines = wrapText(FONT_SM, toast_, 380);
        int h = int(lines.size()) * 16 + 10;
        int y = kHintY - 70 - h;
        c.fillRoundRect(20, y, 390, h, 8, rgb(10, 14, 26), a * 235 / 255);
        c.frameRoundRect(20, y, 390, h, 8, toastColor_, 2, a);
        int ty = y + 5;
        for (const std::string& l : lines) {
            drawText(c, FONT_SM, 30, ty, l, toastColor_, a);
            ty += 16;
        }
    }

    // ------------------------------------------------------------- panels
    void drawTopBar(Canvas& c, const Game& g) {
        c.fillRect(0, 0, W(), kTopH, rgb(5, 8, 16), 235);
        c.hline(0, W() - 1, kTopH - 1, theme::border);
        std::string left = g.draft.active ? "Galaxy setup \xC2\xB7 tile " + std::to_string(std::min<int>(g.draft.placed + 1, g.draft.total)) + " of " +
                                                std::to_string(g.draft.total)
                                          : "Round " + std::to_string(g.round) + " \xC2\xB7 " + phaseName(g.phase);
        int lw = drawText(c, FONT_SMB, 8, 4, left, theme::gold);
        std::string mid;
        uint32_t midCol = theme::text;
        if (session_->humanToAnswer()) {
            mid = g.activePlayer == session_->human() ? "Your turn" : "Your decision";
            midCol = theme::cyan;
        } else if (g.activePlayer >= 0) {
            mid = playerName(g, g.activePlayer) + "'s turn";
            midCol = pcolor(g, g.activePlayer);
        } else if (session_->engine().waiting()) {
            mid = playerName(g, session_->engine().decision().player) + " is deciding";
            midCol = pcolor(g, session_->engine().decision().player);
        }
        int mx = 8 + lw + 16;
        int mw = drawText(c, FONT_SM, mx, 4, fitText(FONT_SM, mid, 150), midCol);
        std::string next = nextPlayerName();
        int leftEnd = mx + mw;
        if (!next.empty()) leftEnd = mx + mw + 8 + drawText(c, FONT_XS, mx + mw + 8, 6, fitText(FONT_XS, "next: " + next, 100), theme::faint);
        int rightStart = W();
        int h = session_->seat();
        if (h >= 0 && h < g.numPlayers) {
            const Player& p = g.players[h];
            char buf[96], tail[48];
            std::snprintf(buf, sizeof buf, "Tactic %d \xC2\xB7 Fleet %d \xC2\xB7 Strat %d \xC2\xB7 ", p.tactic, p.fleet, p.strategy);
            std::snprintf(tail, sizeof tail, "%d \xC2\xB7 %d VP", p.tradeGoods, p.vp);
            if (icons::available()) {
                int tw = textWidth(FONT_SM, tail), x = W() - 8 - tw;
                drawText(c, FONT_SM, x, 4, tail, pcolor(g, h));
                icons::draw(c, "trade_good", x - 16, 3, 14);
                drawTextAligned(c, FONT_SM, 0, 4, x - 18, Align::Right, buf, pcolor(g, h));
                rightStart = x - 18 - textWidth(FONT_SM, buf);
            } else {
                drawTextAligned(c, FONT_SM, 0, 4, W() - 8, Align::Right, std::string(buf) + "TG " + tail, pcolor(g, h));
                rightStart = W() - 8 - textWidth(FONT_SM, std::string(buf) + "TG " + tail);
            }
        }
        // Galactic Events (0.46.2, owner): a button in the free part of the bar while events are in play
        eventsBtn_ = Rect{};
        if (g.events) {
            const std::string label = "Galactic Events";
            const int bw = textWidth(FONT_SM, label) + 18, gap = rightStart - leftEnd;
            if (gap >= bw + 24) {
                const int bx = leftEnd + (gap - bw) / 2;
                eventsBtn_ = Rect{bx, 2, bw, kTopH - 5};
                const bool hot = eventsOpen_ || (hoverX_ >= float(bx) && hoverX_ < float(bx + bw) && hoverY_ >= 0.f && hoverY_ < float(kTopH));
                drawPanel(c, eventsBtn_, hot ? rgb(40, 34, 70) : rgb(20, 18, 40), hot ? theme::gold : rgb(150, 110, 210), 5, 235);
                drawTextAligned(c, FONT_SM, bx, 4, bw, Align::Center, label, hot ? theme::gold : rgb(215, 190, 255));
            }
        }
    }

    // ---- The map button layout (0.47.0, owner): a panel of buttons on the map for the game-menu screens - drag it by
    // its strip, fold it with "-" - and each screen opens as a pop-up window over the map, dragged by its title, closed
    // with x / B / Esc / a right click. The map beside the window still zooms (wheel) and moves (right-drag); clicks
    // beside it do nothing, keys go to the screen inside. Where the panel sits and whether it's folded is remembered.
    // The panel shows once a mouse is used (a controller player keeps the game menu as before).
    static constexpr float kPopK = 0.75f;  // the window shows the 640x480 screen at 75%
    static constexpr int kPopBar = 16;     // its title strip
    static constexpr int kBarW = 96, kBarGrip = 14, kBarBtnH = 18, kBarBtns = 9;
    struct Pop {
        bool on = false;
        int which = -1;
        float x = -1.f, y = -1.f;  // top left (game units); -1: not placed yet
        bool drag = false;
        float gx = 0.f, gy = 0.f;  // where it was grabbed
    } pop_;
    float barX_ = -1.f, barY_ = -1.f, barGX_ = 0.f, barGY_ = 0.f;
    bool barDrag_ = false, barMin_ = false, barLoaded_ = false, barShown_ = false, mouseSeen_ = false;
    std::unique_ptr<Canvas> popCanvas_;
    static const char* barName(int i) {
        static const char* const kNames[kBarBtns] = {"Objectives", "Laws",    "Your cards", "Faction sheet", "Units",
                                                     "Technology", "Planets", "Players",    "Game log"};
        return kNames[i];
    }
    int popW() const { return int(640.f * kPopK + 0.5f); }
    int popH() const { return kPopBar + int(480.f * kPopK + 0.5f); }
    Rect popRect() const { return Rect{int(pop_.x), int(pop_.y), popW(), popH()}; }
    Rect barRect() const { return Rect{int(barX_), int(barY_), kBarW, barMin_ ? kBarGrip : kBarGrip + kBarBtns * (kBarBtnH + 2) + 3}; }
    void clampPop() {
        pop_.x = std::max(0.f, std::min(float(W() - popW()), pop_.x));
        pop_.y = std::max(float(kTopH), std::min(float(kHintY - popH()), pop_.y));
    }
    void clampBar() {
        const Rect r = barRect();
        barX_ = std::max(0.f, std::min(float(W() - r.w), barX_));
        barY_ = std::max(float(kTopH), std::min(float(kHintY - r.h), barY_));
    }
    void saveBar(App& app) {
        app.settings().setInt("bar_x", int(barX_));
        app.settings().setInt("bar_y", int(barY_));
        app.settings().setInt("bar_min", barMin_ ? 1 : 0);
        app.settings().save();
    }
    // Opens screen i in the window (or closes the window when it shows i already).
    void togglePop(int i) {
        const Game& g = session_->game();
        const int h = session_->seat();
        const bool same = pop_.on && pop_.which == i;
        info_.reset();
        overlay_ = Overlay::None;
        fromMenu_ = false;
        eventsOpen_ = false;
        pop_.on = false;
        if (same) {
            audio::play(audio::Sfx::Back);
            return;
        }
        switch (i) {
            case 0: overlay_ = Overlay::Objectives; break;
            case 1: info_ = makeLawScreen(g, h); break;
            case 2: info_ = makeHandScreen(g, h); break;
            case 3: info_ = makeFactionSheet(g, h); break;
            case 4: info_ = makeUnitScreen(g, h); break;
            case 5: info_ = makeTechScreen(g, h); break;
            case 6: info_ = makePlanetScreen(g, h); break;
            case 7: overlay_ = Overlay::Players; break;
            default:
                overlay_ = Overlay::Log;
                logScroll_ = 0;
                break;
        }
        pop_.on = true;
        pop_.which = i;
        if (pop_.x < 0.f) {  // first time: over the middle of the map, left of the panel where there's room
            int x = (mapW() - popW()) / 2;
            if (barShown_) x = std::min(x, int(barX_) - popW() - 6);
            pop_.x = float(std::max(0, x));
            pop_.y = float(kTopH + 6);
        }
        clampPop();
        audio::play(audio::Sfx::Confirm);
    }
    // Dragging the panel or the window (before anything else sees the mouse). True: the mouse is busy dragging.
    bool dragBarAndPop(App& app, const Input& in) {
        const Mouse& m = in.mouse();
        if (barDrag_) {
            if (m.left) {
                barX_ = m.x - barGX_;
                barY_ = m.y - barGY_;
                clampBar();
            } else {
                barDrag_ = false;
                saveBar(app);
            }
            return true;
        }
        if (pop_.drag) {
            if (m.left) {
                pop_.x = m.x - pop_.gx;
                pop_.y = m.y - pop_.gy;
                clampPop();
            } else {
                pop_.drag = false;
            }
            return true;
        }
        if (!m.leftPress) return false;
        if (pop_.on) {
            const Rect r = popRect();
            if (m.x >= float(r.x) && m.x < float(r.x + r.w - 20) && m.y >= float(r.y) && m.y < float(r.y + kPopBar)) {
                pop_.drag = true;
                pop_.gx = m.x - pop_.x;
                pop_.gy = m.y - pop_.y;
                return true;
            }
        }
        if (barShown_) {
            const Rect r = barRect();
            if (m.x >= float(r.x) && m.x < float(r.x + r.w - 16) && m.y >= float(r.y) && m.y < float(r.y + kBarGrip)) {
                barDrag_ = true;
                barGX_ = m.x - barX_;
                barGY_ = m.y - barY_;
                return true;
            }
        }
        return false;
    }
    // The screen in the window gets the keys, and the mouse moved into its own 640x480; beside the window the
    // mouse moves the map.
    void updatePop(App& app, const Input& in, uint32_t now) {
        const Mouse& m = in.mouse();
        const Rect r = popRect();
        const Rect br = barRect();
        const bool onBar = barShown_ && m.x >= float(br.x) && m.x < float(br.x + br.w) && m.y >= float(br.y) && m.y < float(br.y + br.h);
        const bool inside = m.active && !onBar && m.x >= float(r.x) && m.x < float(r.x + r.w) && m.y >= float(r.y) && m.y < float(r.y + r.h);
        if (!inside && !onBar) mouseMap(in, now);
        Input li = in;
        Mouse& lm = li.mouseState();
        if (!inside) {  // (a click beside the window isn't for the screen in it - but the hint bar's buttons are)
            lm.leftPress = false;
            lm.click = false;
        }
        // (info screens take legacyX off the mouse: they are usually drawn centred on a wide screen)
        lm.x = (m.x - float(r.x)) / kPopK + float(theme::legacyX());
        lm.y = (m.y - float(r.y + kPopBar)) / kPopK;
        lm.dragX /= kPopK;
        lm.dragY /= kPopK;
        if (!inside) lm.wheel = 0.f;
        if (info_) {
            if (!info_->update(li)) {
                info_.reset();
                pop_.on = false;
            }
        } else {
            updateOverlay(app, li);
            if (overlay_ == Overlay::None && !info_) pop_.on = false;
        }
    }
    void drawBar(App& app, Canvas& c) {
        if (!mouseSeen_ && !pop_.on) return;  // (a controller-only player has the game menu)
        if (!barLoaded_) {
            barLoaded_ = true;
            barX_ = float(app.settings().getInt("bar_x", -1));
            barY_ = float(app.settings().getInt("bar_y", -1));
            barMin_ = app.settings().getInt("bar_min", W() <= 640 ? 1 : 0) != 0;  // (folded on a 4:3 screen at first)
            if (barX_ < 0.f || barY_ < 0.f) {
                barX_ = float(mapW() - kBarW - 6);
                barY_ = float(kTopH + 6);
            }
        }
        clampBar();
        barShown_ = true;
        const Rect r = barRect();
        drawPanel(c, r, rgb(10, 14, 28), theme::border, 5, 232);
        hits::addAction(r, [] {});  // (a click on the panel itself isn't for the map)
        // the grip: drag it; "-" folds the panel, "+" opens it
        c.fillRect(r.x + 2, r.y + 2, r.w - 4, kBarGrip - 3, rgb(30, 40, 72), 230);
        for (int k = 0; k < 3; ++k) c.fillRect(r.x + 6 + k * 4, r.y + 5, 2, 5, theme::dim);
        drawText(c, FONT_XS, r.x + 20, r.y + 2, "SCREENS", theme::dim);
        const Rect fold{r.x + r.w - 16, r.y + 1, 15, kBarGrip - 2};
        const bool foldHot = hoverX_ >= float(fold.x) && hoverX_ < float(fold.x + fold.w) && hoverY_ >= float(fold.y) && hoverY_ < float(fold.y + fold.h);
        drawTextAligned(c, FONT_SMB, fold.x, fold.y - 2, fold.w, Align::Center, barMin_ ? "+" : "-", foldHot ? theme::gold : theme::text);
        hits::addAction(fold, [this, &app] {
            barMin_ = !barMin_;
            clampBar();
            saveBar(app);
            audio::play(audio::Sfx::Toggle);
        });
        if (barMin_) return;
        for (int i = 0; i < kBarBtns; ++i) {
            const Rect b{r.x + 3, r.y + kBarGrip + 2 + i * (kBarBtnH + 2), r.w - 6, kBarBtnH};
            const bool hot = hoverX_ >= float(b.x) && hoverX_ < float(b.x + b.w) && hoverY_ >= float(b.y) && hoverY_ < float(b.y + b.h);
            const bool open = pop_.on && pop_.which == i;
            drawPanel(c, b, open ? rgb(60, 48, 20) : hot ? rgb(34, 46, 80) : rgb(20, 26, 46), open || hot ? theme::gold : theme::border, 4, 240);
            drawTextAligned(c, FONT_SM, b.x, b.y + 2, b.w, Align::Center, barName(i), open || hot ? theme::gold : theme::text);
            hits::addAction(b, [this, i] { togglePop(i); });
        }
    }
    // The window: its title strip (drag it, x closes), the screen drawn at 75% into a canvas of its own and copied in.
    void drawPop(App& app, Canvas& c, const Game& g, uint32_t now) {
        hits::addAction(Rect{0, 0, W(), kHintY}, [] {});  // (clicks beside the window do nothing; the hint bar still works)
        clampPop();
        const Rect r = popRect();
        c.fillRect(r.x + 5, r.y + 6, r.w, r.h, rgb(0, 0, 0), 120);  // a shadow
        drawPanel(c, Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, rgb(14, 18, 32), rgb(150, 120, 60), 6, 255);
        c.fillRect(r.x, r.y, r.w, kPopBar, pop_.drag ? rgb(50, 60, 100) : rgb(32, 40, 72), 255);
        for (int k = 0; k < 3; ++k) c.fillRect(r.x + 6 + k * 4, r.y + 5, 2, 6, theme::dim);
        drawText(c, FONT_SMB, r.x + 22, r.y + 1, pop_.which >= 0 ? barName(pop_.which) : "", theme::gold);
        drawTextAligned(c, FONT_XS, r.x, r.y + 3, r.w - 26, Align::Right, "drag to move", theme::faint);
        const Rect x{r.x + r.w - 18, r.y + 1, 16, kPopBar - 2};
        const bool xHot = hoverX_ >= float(x.x) && hoverX_ < float(x.x + x.w) && hoverY_ >= float(x.y) && hoverY_ < float(x.y + x.h);
        if (xHot) c.fillRect(x.x, x.y, x.w, x.h, rgb(140, 40, 40), 220);
        drawTextAligned(c, FONT_SMB, x.x, x.y - 1, x.w, Align::Center, "x", xHot ? theme::text : theme::dim);
        hits::addAction(Rect{r.x, r.y, r.w, r.h}, [] {});
        hits::addAction(x, [this] {
            info_.reset();
            overlay_ = Overlay::None;
            pop_.on = false;
            audio::play(audio::Sfx::Back);
        });
        // the screen itself
        const float s = c.scale() * kPopK;
        if (!popCanvas_ || std::fabs(popCanvas_->scale() - s) > 1e-4f) popCanvas_ = std::make_unique<Canvas>(640, 480, s);
        Canvas& pc = *popCanvas_;
        pc.resetClip();
        pc.clear(rgb(6, 9, 18));
        const int ox = hits::origin(), oy = hits::originY();
        const float ok = hits::scale();
        hits::origin() = r.x;
        hits::originY() = r.y + kPopBar;
        hits::scale() = kPopK;
        if (info_) info_->draw(pc, now);
        else drawOverlay(app, pc, g);
        hits::origin() = ox;
        hits::originY() = oy;
        hits::scale() = ok;
        c.pxCopy(pc, c.X(r.x), c.Y(r.y + kPopBar));
        drawBar(app, c);  // (the panel stays on top, to switch screens)
    }

    // Waypoint Mode (0.56.0, owner: "a button option on the bottom right of the screen (before the right window, but
    // above the game log) ... a cross-hair as its icon, with a tooltip that says Waypoint Mode"): shown while you move
    // ships; a click (or X) switches the move editor to picking the route on the map and back.
    Rect wpBtn_{};
    void drawWaypointButton(Canvas& c) {
        wpBtn_ = Rect{};
        if (!ui_ || peek_ || !ui_->waypointsAvailable() || !session_->humanToAnswer()) return;
        constexpr int kB = 26;
        const int feedTop = kHintY - (3 * 14 + 6) - 2;  // (the game log's top while a question is on screen)
        wpBtn_ = Rect{mapW() - kB - 6, feedTop - kB - 6, kB, kB};
        // a 4:3 screen: the move list covers the map's width - the button sits at its corner, over the gap to the log
        const Rect dp = decisionPanelRect();
        if (wpBtn_.x < dp.x + dp.w + 4) wpBtn_.y = (dp.y + dp.h + feedTop) / 2 - kB / 2;
        const Rect& b = wpBtn_;
        const bool on = ui_->waypointsOn();
        const bool hot = hoverX_ >= float(b.x) && hoverX_ < float(b.x + b.w) && hoverY_ >= float(b.y) && hoverY_ < float(b.y + b.h);
        drawPanel(c, b, on ? rgb(60, 48, 20) : hot ? rgb(34, 46, 80) : rgb(16, 22, 40), on || hot ? theme::gold : rgb(110, 150, 200), 6, 235);
        // the cross-hair: a ring, four ticks through it, a dot in the middle
        const float cx = float(b.x) + float(b.w) / 2.f, cy = float(b.y) + float(b.h) / 2.f, r = float(kB) * 0.28f;
        const uint32_t col = on || hot ? theme::gold : rgb(200, 220, 245);
        for (int k = 0; k < 32; ++k) {
            const float a0 = float(k) / 32.f * 6.2831853f, a1 = float(k + 1) / 32.f * 6.2831853f;
            c.line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, col, 1.6f, 255);
        }
        const float in = r * 0.45f, out = r * 1.45f;
        c.line(cx - out, cy, cx - in, cy, col, 1.6f, 255);
        c.line(cx + in, cy, cx + out, cy, col, 1.6f, 255);
        c.line(cx, cy - out, cx, cy - in, col, 1.6f, 255);
        c.line(cx, cy + in, cx, cy + out, col, 1.6f, 255);
        c.fillCircle(cx, cy, 1.6f, col, 255);
        hits::addAction(b, [this] {
            if (!ui_) return;
            ui_->toggleWaypoints();
            audio::play(audio::Sfx::Toggle);
        });
        if (hot) {  // the tooltip, to the left of the button
            const std::string tip = "Waypoint Mode";
            const int tw = textWidth(FONT_SMB, tip) + 16, th = 20;
            const Rect t{b.x - tw - 6, b.y + (b.h - th) / 2, tw, th};
            drawPanel(c, t, rgb(14, 12, 30), theme::gold, 5, 245);
            drawTextAligned(c, FONT_SMB, t.x, t.y + 3, t.w, Align::Center, tip, theme::gold);
        }
    }

    // The Galactic Events pop-up (0.46.2, owner): the events in play, each one's text when the mouse is over it;
    // a click outside (or B / Esc) closes it. Its clicks are registered last so they sit on top.
    bool eventsOpen_ = false;
    Rect eventsBtn_{};
    float hoverX_ = -1.f, hoverY_ = -1.f;
    void drawEvents(Canvas& c, const Game& g) {
        if (!g.events || eventsBtn_.w <= 0) return;
        if (!eventsOpen_) {
            hits::addAction(eventsBtn_, [this] {
                eventsOpen_ = true;
                audio::play(audio::Sfx::Confirm);
            });
            return;
        }
        std::vector<int> evs;
        for (int e = 0; e < kEventCount; ++e)
            if (eventOn(g, e)) evs.push_back(e);
        const int rowH = 20, pw = 250, ph = 26 + rowH * int(evs.size()) + 6;
        const int px = std::max(4, std::min(W() - pw - 4, eventsBtn_.x + eventsBtn_.w / 2 - pw / 2)), py = kTopH + 2;
        hits::addAction(Rect{0, 0, W(), 480}, [this] { eventsOpen_ = false; });  // (a click elsewhere closes it)
        hits::addAction(eventsBtn_, [this] { eventsOpen_ = false; });
        const Rect panel{px, py, pw, ph};
        drawPanel(c, panel, rgb(12, 10, 26), rgb(150, 110, 210), 8, 245);
        hits::addAction(panel, [] {});
        drawText(c, FONT_SMB, px + 10, py + 6, "Galactic events in this game", theme::gold);
        int over = -1;
        for (size_t i = 0; i < evs.size(); ++i) {
            const Rect row{px + 6, py + 26 + rowH * int(i), pw - 12, rowH};
            const bool hot = hoverX_ >= float(row.x) && hoverX_ < float(row.x + row.w) && hoverY_ >= float(row.y) && hoverY_ < float(row.y + row.h);
            if (hot) {
                over = evs[i];
                c.fillRect(row.x, row.y, row.w, row.h, rgb(60, 44, 100), 200);
            }
            drawText(c, FONT_SM, row.x + 6, row.y + 3, fitText(FONT_SM, galacticEvent(evs[i]).name, row.w - 12), hot ? theme::gold : theme::text);
            hits::addAction(row, [] {});
        }
        if (over < 0) {
            drawTextAligned(c, FONT_XS, px, py + ph + 2, pw, Align::Center, "Point at an event to read it", theme::faint);
            return;
        }
        // the tooltip: beside the pop-up where there is room, else under it
        const int tw = 280;
        const std::vector<std::string> lines = wrapText(FONT_SM, galacticEvent(over).text, tw - 20);
        const int th = 28 + 15 * int(lines.size()) + 8;
        int tx = px + pw + 6, ty = py;
        if (tx + tw > W() - 4) tx = px - tw - 6;
        if (tx < 4) {
            tx = std::max(4, std::min(W() - tw - 4, px));
            ty = py + ph + 6;
        }
        drawPanel(c, Rect{tx, ty, tw, th}, rgb(14, 12, 30), theme::gold, 8, 250);
        drawText(c, FONT_SMB, tx + 10, ty + 6, fitText(FONT_SMB, galacticEvent(over).name, tw - 20), theme::gold);
        for (size_t i = 0; i < lines.size(); ++i) drawText(c, FONT_SM, tx + 10, ty + 26 + 15 * int(i), lines[i], theme::text);
    }

    // Choosing a strategy card: the highlighted card and both its abilities.
    // 0.29.0 (owner): when a question is about using a leader, its card is shown beside it to read first.
    int leaderInPrompt(const Decision& d) const {
        if (d.prompt == leaderPromptKey_) return leaderPromptHit_;
        leaderPromptKey_ = d.prompt;
        leaderPromptHit_ = -1;
        const std::string text = d.prompt + " " + d.context;
        size_t bestLen = 0;
        for (int i = 0; i < kLeaderCount; ++i) {
            const std::string n = plainName(kLeaders[i].name);  // (0.43.3: prompts may name it without the Omega sign)
            if (n.size() > 3 && n.size() > bestLen && text.find(n) != std::string::npos) {
                leaderPromptHit_ = i;
                bestLen = n.size();
            }
        }
        return leaderPromptHit_;
    }
    mutable std::string leaderPromptKey_;
    mutable int leaderPromptHit_ = -1;
    void drawLeaderPreview(Canvas& c, const Game& g, int leader) {
        (void)g;
        const Rect r{mapW() + 6, kTopH + 4, W() - mapW() - 10, kHintY - kTopH - 8};
        drawPanel(c, r, theme::panel, theme::border, 8, kSideAlpha + 40);
        const LeaderDef& L = kLeaders[leader];
        static const char* kType[3] = {"AGENT", "COMMANDER", "HERO"};
        const uint32_t base = L.type == 0 ? rgb(90, 170, 120) : L.type == 1 ? rgb(90, 140, 210) : rgb(200, 150, 60);
        c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, 24, 6, shade(base, 0.6f));
        drawTextAligned(c, FONT_MDB, r.x, r.y + 8, r.w, Align::Center, kType[L.type < 0 || L.type > 2 ? 0 : L.type], theme::text);
        int y = r.y + 36;
        if (L.faction >= 0) {
            drawFactionIcon(c, L.faction, r.x + 8, y, 28);
            drawText(c, FONT_XS, r.x + 42, y + 16, factionShortName(L.faction), theme::dim);
        }
        for (const std::string& l : wrapText(FONT_LG, L.name, r.w - 50)) {
            drawText(c, FONT_LG, r.x + 42, y - 2, l, theme::gold);
            y += 18;
        }
        y = std::max(y, r.y + 72) + 6;
        for (const std::string& l : wrapText(FONT_MD, leaderText(leader), r.w - 16)) {
            if (y > r.y + r.h - 20) break;
            drawText(c, FONT_MD, r.x + 8, y, l, theme::text);
            y += 16;
        }
        drawTextAligned(c, FONT_XS, r.x, r.y + r.h - 16, r.w, Align::Center, "Read it, then answer on the left.", theme::faint);
    }
    void drawStrategyPreview(Canvas& c, const Game& g, int card) {
        const Rect r{mapW() + 6, kTopH + 4, W() - mapW() - 10, kHintY - kTopH - 8};
        drawPanel(c, r, theme::panel, theme::border, 8, kSideAlpha);
        if (card < 0 || card >= kStrategyCards) return;
        const int cw = r.w - 8;
        cards::strategy(c, card, r.x + 4, r.y + 4, cw);
        int y = r.y + 8 + cards::strategyHeight(cw);
        const int x = r.x + 8, w = r.w - 16;
        if (g.cardTradeGoods[card]) {
            drawText(c, FONT_SMB, x, y, "+" + std::to_string(g.cardTradeGoods[card]) + " trade goods on this card", theme::gold);
            y += 17;
        }
        drawText(c, FONT_SMB, x, y, "Primary", strategyCardColor(card));
        y += 15;
        for (const std::string& l : wrapText(FONT_XS, strategyPrimary(card), w)) {
            drawText(c, FONT_XS, x, y, l, theme::text);
            y += 13;
        }
        y += 6;
        drawText(c, FONT_SMB, x, y, "Secondary", strategyCardColor(card));
        y += 15;
        for (const std::string& l : wrapText(FONT_XS, strategySecondary(card), w)) {
            if (y + 13 > r.y + r.h) break;
            drawText(c, FONT_XS, x, y, l, theme::dim);
            y += 13;
        }
    }

    void drawSystemPanel(Canvas& c, const Game& g) {
        const Rect p{mapW() + 4, kTopH + 4, W() - mapW() - 8, kHintY - kTopH - 8};
        drawPanel(c, p, theme::panel, theme::border, 8, kSideAlpha);
        if (const int q = map_.infoPlayer(); q >= 0 && q < g.numPlayers) {  // on a faction's info tiles
            const Player& pl = g.players[size_t(q)];
            int x = p.x + 8, w = p.w - 16, y = p.y + 6;
            drawFactionIcon(c, pl.faction, x, y, 26);
            drawText(c, FONT_MDB, x + 32, y + 4, fitText(FONT_MDB, playerName(g, q) + (q == session_->seat() ? " (you)" : ""), w - 32),
                     pcolor(g, q));
            y += 32;
            auto row = [&](const std::string& a, const std::string& b) {
                drawText(c, FONT_SM, x, y, a, theme::dim);
                drawTextAligned(c, FONT_SMB, x, y, w, Align::Right, b, theme::text);
                y += 17;
            };
            row("Victory points", std::to_string(pl.vp) + " / " + std::to_string(g.vpGoal));
            row("Command tokens", std::to_string(pl.tactic) + " / " + std::to_string(pl.fleet) + " / " + std::to_string(pl.strategy));
            row("Trade goods", std::to_string(pl.tradeGoods));
            row("Commodities", std::to_string(pl.commodities) + " / " + std::to_string(commodityValue(g, pl)));
            row("Action cards", std::to_string(pl.actionCards.size()));
            row("Secret objectives", std::to_string(pl.secrets.size()) + " held");
            row("Technology", std::to_string(pl.techs.size()));
            std::string cards;
            for (int8_t cd : pl.cards) cards += (cards.empty() ? "" : ", ") + std::to_string(cd + 1) + " " + strategyName(cd);
            row("Strategy", cards.empty() ? "-" : fitText(FONT_SMB, cards, w - 70));
            // the play area (0.43.0): what this player has in play, a small card and a line each
            const std::vector<playarea::Item> items = playarea::of(g, q, session_->seat());
            if (!items.empty()) {
                y += 4;
                drawText(c, FONT_SMB, x, y, "In play", theme::gold);
                y += 16;
                for (const playarea::Item& it : items) {
                    if (y > p.y + p.h - 44) {
                        drawText(c, FONT_XS, x, y, "...", theme::dim);
                        y += 13;
                        break;
                    }
                    playarea::drawCard(c, it, x, y, 16, 20, 255, lastNow_);
                    drawText(c, FONT_XS, x + 22, y, fitText(FONT_XS, it.name, w - 22), it.glow ? rgb(140, 225, 255) : theme::text);
                    drawText(c, FONT_XS, x + 22, y + 10, fitText(FONT_XS, it.detail, w - 22), theme::dim);
                    y += 23;
                }
            }
            y += 8;
            for (const std::string& l : wrapText(FONT_XS, keyText("A: faction sheet, secrets and technology."), w)) {
                drawText(c, FONT_XS, x, y, l, theme::faint);
                y += 13;
            }
            return;
        }
        int s = map_.cursor();
        if (const decor::Tile* t = map_.decorAt(g, s)) {  // a Thunder's Edge placeholder (0.24.0)
            int x = p.x + 8, w = p.w - 16, y = p.y + 6;
            drawText(c, FONT_MDB, x, y, fitText(FONT_MDB, decor::name(*t), w), t->kind == decor::ThundersEdge ? rgb(230, 200, 120) : rgb(255, 120, 170));
            y += 22;
            for (const std::string& l : wrapText(FONT_SMB, decor::subtitle(g, *t), w)) {  // 0.44.3: what it is in this game
                drawText(c, FONT_SMB, x, y, l, t->kind == decor::ThundersEdge ? rgb(230, 200, 120) : rgb(255, 140, 150));
                y += 16;
            }
            y += 2;
            for (const std::string& l : wrapText(FONT_XS, decor::about(g, *t), w)) {
                drawText(c, FONT_XS, x, y, l, theme::dim);
                y += 13;
            }
            for (const std::string& l : wrapText(FONT_XS, keyText("A: the tile, large."), w)) {
                drawText(c, FONT_XS, x, y + 4, l, theme::faint);
                y += 13;
            }
            return;
        }
        if (s < 0 || s >= int(g.systems.size())) return;
        const SystemState& sy = g.systems[s];
        const SystemDef& d = sy.d();
        int x = p.x + 8, w = p.w - 16, y = p.y + 6;
        std::string title = sy.home >= 0 ? playerName(g, sy.home) + " home" : d.name;
        drawText(c, FONT_MDB, x, y, fitText(FONT_MDB, title, w), sy.home >= 0 ? pcolor(g, sy.home) : theme::gold);
        y += 19;
        std::string kind = std::string("Tile ") + d.key;
        if (d.anomalies & kAsteroid) kind += " \xC2\xB7 asteroid field";
        if (d.anomalies & kNebula) kind += " \xC2\xB7 nebula";
        if (d.anomalies & kSupernova) kind += " \xC2\xB7 supernova";
        if (d.anomalies & kRift) kind += " \xC2\xB7 gravity rift";
        uint8_t wh = wormholesOf(g, s);
        if (wh & kAlpha) kind += " \xC2\xB7 \xCE\xB1 wormhole";
        if (wh & kBeta) kind += " \xC2\xB7 \xCE\xB2 wormhole";
        if (wh & kGamma) kind += " \xC2\xB7 \xCE\xB3 wormhole";
        if (wh & kDelta) kind += " \xC2\xB7 \xCE\xB4 wormhole";
        if (s == g.nexus) kind += sy.nexusActive ? " \xC2\xB7 wormhole nexus (active)" : " \xC2\xB7 wormhole nexus (inactive: gamma only)";
        if (s == g.ionStorm) kind += " \xC2\xB7 ion storm token (flips when used)";
        if (sy.frontier) kind += " \xC2\xB7 frontier token (Dark Energy Tap / Exploration Probe explore it)";
        for (const std::string& l : wrapText(FONT_XS, kind, w)) {
            drawText(c, FONT_XS, x, y, l, theme::dim);
            y += 13;
        }
        // 0.43.0 (owner): what the anomaly does, in red (as the viewer sees it - Veldyr home nebula, scars...)
        {
            const uint8_t an = anomaliesFor(g, session_->seat(), s);
            std::string rule;
            if (an & kAsteroid) rule += "Asteroid field: ships can't move into or through it (Antimass Deflectors let them). ";
            if (an & kNebula) rule += "Nebula: ships move in only when it is the active system; ships leaving it move 1; the defender gets +1 on combat rolls. ";
            if (an & kSupernova) rule += "Supernova: ships can't move into or through it. ";
            if ((an & kRift) || dimensionalTear(g, s))
                rule += "Gravity rift: ships moving out of or through it get +1 move; each rolls as it leaves - on 1 to 3 it is removed from the board, with what it carries. ";
            if (scarAt(g, s)) rule += "Entropic scar: no unit abilities in it or against units in it (space cannon, barrage, bombardment, sustain damage, production...). ";
            if (sy.fracture) rule += "The Fracture (active - in play): reached through the ingress and egress points. ";
            for (const std::string& l : wrapText(FONT_XS, rule, w)) {
                drawText(c, FONT_XS, x, y, l, rgb(240, 90, 80));
                y += 13;
            }
        }
        if (sy.custodians) {
            drawText(c, FONT_XS, x, y, "Custodians token: 6 influence to remove", rgb(240, 220, 150));
            y += 13;
        }
        if (std::find(g.dsGlory.begin(), g.dsGlory.end(), int16_t(s)) != g.dsGlory.end()) {  // Discordant Stars (0.39.0)
            drawText(c, FONT_XS, x, y, "Glory token: natural 10s in combat here make 1 more hit", rgb(240, 160, 120));
            y += 13;
        }
        y += 3;
        for (int16_t pl : sy.planets) {
            const PlanetState& ps = g.planets[pl];
            const PlanetDef& pd = kPlanets[ps.def];
            c.hline(x, x + w, y, theme::border);
            y += 3;
            // name, then trait / specialty / legendary icons, then resources and influence
            std::vector<std::string> marks;
            const std::vector<Trait> traits = traitsOf(g, pl);  // (owner 2026-11-13: as they are now - Terraform gives all three)
            for (Trait tr : traits) {  // (Thunder's Edge: some have two)
                if (tr == Trait::Cultural) marks.push_back("cultural");
                if (tr == Trait::Hazardous) marks.push_back("hazardous");
                if (tr == Trait::Industrial) marks.push_back("industrial");
            }
            static const char* kSkip[4] = {"propulsion", "biotic", "warfare", "cybernetic"};
            for (int sc = 0; sc < 4; ++sc)
                if ((planetSkips(g, pl) >> sc) & 1) marks.push_back(kSkip[sc]);
            if (pd.skip2 != TechColor::None && pd.skip2 == pd.skip) marks.push_back(kSkip[int(pd.skip2) & 3]);
            if (isLegendaryPlanet(g, pl)) marks.push_back("legendary_complete");
            int markW = icons::available() ? int(marks.size()) * 15 : 0;
            int nw = drawText(c, FONT_SMB, x, y, fitText(FONT_SMB, pd.name, w - 50 - markW), ps.exhausted ? theme::dim : theme::text);
            if (markW)
                for (size_t k = 0; k < marks.size(); ++k) icons::draw(c, marks[k], x + nw + 4 + int(k) * 15, y + 1, 14, ps.exhausted ? 150 : 255);
            drawResourceIcon(c, x + w - 32, y + 7, 19, planetResources(g, pl), FONT_XS, false);
            drawResourceIcon(c, x + w - 9, y + 7, 19, planetInfluence(g, pl), FONT_XS, true);
            y += 16;
            std::string sub;
            if (ps.owner >= 0) sub = playerName(g, ps.owner) + (ps.exhausted ? " (exhausted)" : "");
            else sub = "Unclaimed";
            for (size_t k = 0; k < traits.size(); ++k) sub += std::string(k ? "/" : " \xC2\xB7 ") + traitName(traits[k]);
            if (pd.flags & kPlanetStation) sub += " \xC2\xB7 space station";
            for (int sc = 0; sc < 4; ++sc)
                if ((planetSkips(g, pl) >> sc) & 1)
                    sub += std::string(" \xC2\xB7 ") + (pd.skip == TechColor(sc) && pd.skip2 == TechColor(sc) ? "2 " : "") + colorName(TechColor(sc)) + " skip";
            if (isLegendaryPlanet(g, pl)) sub += " \xC2\xB7 legendary";
            if (ps.sleeper) sub += " \xC2\xB7 sleeper token";  // (Titans: Terragenesis / Awaken)
            int sx = x;
            if (ps.owner >= 0) {  // the owner's control token
                cards::controlToken(c, g.players[ps.owner].faction, pcolor(g, ps.owner), x + 6, y + 6, 12);
                sx += 15;
            }
            drawText(c, FONT_XS, sx, y, fitText(FONT_XS, sub, w - (sx - x)), ps.owner >= 0 ? pcolor(g, ps.owner) : theme::faint);
            y += 13;
            bool branch = false, trap = false;  // Branch Offices (Veldyr), Traps (Li-Zho) - Discordant Stars 0.42.0
            for (int16_t b : g.dsBranch) branch = branch || b == pl;
            for (int16_t t : g.dsTrap) trap = trap || t == pl;
            if (!ps.attachments.empty() || pl == g.dsBasePlanet || pl == g.dsKeyPlanet || branch || trap || ps.terraform || ps.ulHero ||
                std::find(g.dsCores.begin(), g.dsCores.end(), pl) != g.dsCores.end()) {
                std::string at;
                for (int16_t a : ps.attachments) at += (at.empty() ? "" : ", ") + std::string(kExploreCards[a].name);
                if (ps.terraform) at += std::string(at.empty() ? "" : ", ") + "Terraform (+1/+1, all three traits)";  // (Titans note)
                if (ps.ulHero) at += std::string(at.empty() ? "" : ", ") + "Ul the Progenitor (+3/+3, Space Cannon 5 x3)";
                for (int k = 0; k < 4; ++k)
                    if (g.dsBranch[k] == pl) {
                        const int vd = playerOfFaction(g, "veldyr");
                        at += (at.empty() ? "" : ", ") + std::string(vd >= 0 ? factionNoteName(g.players[size_t(vd)].faction, kNoteFaction + k) : "Branch Office");
                    }
                const int lz = playerOfFaction(g, "lizho");
                for (int k = 0; k < 5; ++k)  // (face down: the Li-Zho player sees which)
                    if (g.dsTrap[k] == pl) at += (at.empty() ? "" : ", ") + (lz >= 0 && g.players[size_t(lz)].human ? std::string("Trap: ") + trapName(k) : std::string("a Trap (face down)"));
                if (std::find(g.dsCores.begin(), g.dsCores.end(), pl) != g.dsCores.end()) at += (at.empty() ? "" : ", ") + std::string("Core token");
                if (pl == g.dsBasePlanet) at += (at.empty() ? "" : ", ") + std::string("Gledge Base");
                if (pl == g.dsKeyPlanet) at += (at.empty() ? "" : ", ") + std::string("Encryption Key");
                for (const std::string& l : wrapText(FONT_XS, "Attached: " + at, w)) {
                    drawText(c, FONT_XS, x, y, l, theme::gold);
                    y += 13;
                }
            }
            for (int q = 0; q < kSides; ++q) {  // (0.45.0: neutral units too - the Fracture's guards)
                if ((q >= g.numPlayers && q != kNeutral) || ps.units[q].empty()) continue;
                for (const std::string& l : wrapText(FONT_XS, (q == kNeutral ? "Neutral: " : "") + forceText(ps.units[q]), w - 6)) {
                    drawText(c, FONT_XS, x + 6, y, l, pcolor(g, q));
                    y += 13;
                }
            }
            if (y > p.y + p.h - 40) break;
        }
        bool header = false;
        for (int q = 0; q < kSides; ++q) {  // (0.45.0, tester: neutral ships too - the Fracture's guards weren't shown)
            if ((q >= g.numPlayers && q != kNeutral) || sy.space[q].empty()) continue;
            if (!header) {
                c.hline(x, x + w, y + 1, theme::border);
                y += 4;
                drawText(c, FONT_SMB, x, y, "In space", theme::cyan);
                y += 15;
                header = true;
            }
            drawText(c, FONT_XS, x, y, playerName(g, q) + ":", pcolor(g, q));
            y += 13;
            for (const std::string& l : wrapText(FONT_XS, forceText(sy.space[q]), w - 6)) {
                if (y > p.y + p.h - 14) break;
                drawText(c, FONT_XS, x + 6, y, l, theme::text);
                y += 13;
            }
        }
        std::string tok;
        for (int q = 0; q < g.numPlayers; ++q)
            if (sy.hasToken(q)) tok += (tok.empty() ? "" : ", ") + playerName(g, q);
        if (!tok.empty() && y < p.y + p.h - 14)
            drawText(c, FONT_XS, x, p.y + p.h - 15, fitText(FONT_XS, "Activated by: " + tok, w), theme::dim);
    }

    void drawPlayersPanel(Canvas& c, const Game& g) {
        const Rect p{mapW() + 4, kTopH + 4, W() - mapW() - 8, kHintY - kTopH - 8};
        drawPanel(c, p, theme::panel, theme::border, 8, kSideAlpha);
        int x = p.x + 8, w = p.w - 16, y = p.y + 6;
        drawText(c, FONT_MDB, x, y, "Players", theme::gold);
        drawTextAligned(c, FONT_XS, x, y + 4, w, Align::Right, "turn order", theme::faint);
        y += 22;
        std::vector<int> order = initiativeOrder(g);
        int rowH = std::min(48, (p.h - 30) / std::max(1, g.numPlayers));
        for (int q : order) {
            const Player& pl = g.players[q];
            if (q == g.activePlayer) c.fillRoundRect(x - 4, y - 1, w + 8, rowH - 3, 5, theme::panelHi);
            c.fillRect(x, y + 2, 4, rowH - 6, pcolor(g, q));
            const int is = icons::available() ? 15 : 0;
            if (is) drawFactionIcon(c, pl.faction, x + 7, y, is, pl.passed ? 140 : 255);
            std::string name = playerName(g, q) + (q == session_->human() ? " (you)" : "") + (q == g.speaker ? " \xE2\x98\x85" : "");
            drawText(c, FONT_SMB, x + 8 + (is ? is + 3 : 0), y, fitText(FONT_SMB, name, w - 50 - (is ? is + 3 : 0)), pl.passed ? theme::dim : pcolor(g, q));
            drawTextAligned(c, FONT_SMB, x, y, w, Align::Right, std::to_string(pl.vp) + " VP", theme::gold);
            std::string cards;
            for (int8_t cd : pl.cards) {
                bool used = (pl.cardsUsed >> cd) & 1;
                cards += std::string(cards.empty() ? "" : " ") + std::to_string(cd + 1) + strategyName(cd)[0] + (used ? "\xE2\x9C\x93" : "");
            }
            if (pl.passed) cards += " passed";
            char buf[96];
            std::snprintf(buf, sizeof buf, "T%d F%d S%d  TG%d  AC%zu  %zu planets", pl.tactic, pl.fleet, pl.strategy, pl.tradeGoods,
                          pl.actionCards.size(), controlledPlanets(g, q).size());
            if (rowH >= 40) {
                drawText(c, FONT_XS, x + 8, y + 15, fitText(FONT_XS, cards.empty() ? "-" : cards, w - 8), theme::dim);
                drawText(c, FONT_XS, x + 8, y + 28, fitText(FONT_XS, buf, w - 8), theme::faint);
            } else {
                drawText(c, FONT_XS, x + 8, y + 15, fitText(FONT_XS, cards + "  " + buf, w - 8), theme::dim);
            }
            y += rowH;
        }
    }

    // Side panel showing a fight as it happens: both sides, dice, result.
    void drawCombatPanel(Canvas& c, const Game& g, uint32_t now) {
        const Rect p{mapW() + 4, kTopH + 4, W() - mapW() - 8, kHintY - kTopH - 8};
        drawPanel(c, p, rgb(26, 14, 18), rgb(150, 50, 50), 8, kSideAlpha + 30);
        int x = p.x + 8, w = p.w - 16, y = p.y + 6;
        std::string title = combat_.title;
        for (char& ch : title) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        drawText(c, FONT_MDB, x, y, fitText(FONT_MDB, title, w), rgb(255, 110, 100));
        y += 19;
        drawText(c, FONT_XS, x, y, fitText(FONT_XS, combat_.where, w), theme::dim);
        y += 16;
        if (!combat_.fireOnly) {
            auto side = [&](int pl, const Force& f, const char* role) {
                c.fillRoundRect(x - 2, y, w + 4, 2, 1, pcolor(g, pl));
                y += 4;
                int ix = icons::available() ? 18 : 0;
                if (ix && pl >= 0 && pl < g.numPlayers) drawFactionIcon(c, g.players[pl].faction, x, y - 1, 16);
                drawText(c, FONT_SMB, x + ix, y, fitText(FONT_SMB, playerName(g, pl), w - 60 - ix), pcolor(g, pl));
                drawTextAligned(c, FONT_XS, x, y + 2, w, Align::Right, role, theme::faint);
                y += 16;
                std::string t = forceText(f);
                if (t.empty()) t = combat_.space ? "no ships left" : "no ground forces left";
                int n = 0;
                for (const std::string& l : wrapText(FONT_XS, t, w - 4)) {
                    if (n++ >= 4) break;
                    drawText(c, FONT_XS, x + 4, y, l, f.empty() ? theme::faint : theme::text);
                    y += 13;
                }
                y += 4;
            };
            side(combat_.pa, combat_.a, "attacker");
            drawTextAligned(c, FONT_SMB, x, y - 2, w, Align::Center, "vs", rgb(255, 110, 100));
            y += 12;
            side(combat_.pb, combat_.b, "defender");
        }
        c.hline(x, x + w, y, rgb(90, 40, 40));
        y += 5;
        // dice, newest last
        int resultH = combat_.ended && !combat_.fireOnly ? 40 : 0;
        int room = p.y + p.h - 6 - resultH - y;
        int per = 40;
        int first = std::max(0, int(combat_.rolls.size()) - std::max(1, room / per));
        for (size_t i = size_t(first); i < combat_.rolls.size(); ++i) {
            const Roll& r = combat_.rolls[i];
            drawText(c, FONT_XS, x, y, fitText(FONT_XS, playerName(g, r.player) + " \xC2\xB7 " + r.label, w), pcolor(g, r.player));
            y += 14;
            int dx = x;
            dice::Timing tm;  // (0.22.0: little ten-sided dice that tumble for a moment)
            tm.start = r.time;
            tm.roll = 450;
            for (size_t k = 0; k < r.dice.size(); ++k) {
                if (dx + 18 > x + w - 34) break;
                bool hit = r.dice[k] >= r.need[k];
                dice::drawD10(c, float(dx + 9), float(y + 10), 21.f, r.dice[k], hit, tm.spin(now, int(k), int(r.dice.size())), -1);
                dx += 20;
            }
            if (r.dice.empty()) drawText(c, FONT_XS, dx, y + 3, "no dice", theme::faint);
            drawTextAligned(c, FONT_SMB, x, y + 1, w, Align::Right,
                            std::to_string(r.hits) + (r.hits == 1 ? " hit" : " hits"), r.hits ? rgb(255, 140, 120) : theme::faint);
            y += 24;
        }
        if (!combat_.lastLoss.empty() && y < p.y + p.h - resultH - 16) {
            drawText(c, FONT_XS, x, y, fitText(FONT_XS, combat_.lastLoss, w), theme::dim);
        }
        if (resultH) {
            int ry = p.y + p.h - resultH - 4;
            uint32_t wc = combat_.winner >= 0 ? pcolor(g, combat_.winner) : theme::dim;
            c.fillRoundRect(x - 2, ry, w + 4, resultH, 6, shade(wc, 0.3f));
            c.frameRoundRect(x - 2, ry, w + 4, resultH, 6, wc, 2);
            std::string t = combat_.winner >= 0 ? (combat_.winner == session_->human() ? "You win!" : playerName(g, combat_.winner) + " wins")
                                                 : "No winner";
            drawTextAligned(c, FONT_MDB, x, ry + 3, w, Align::Center, fitText(FONT_MDB, t, w), theme::text);
            drawTextAligned(c, FONT_XS, x, ry + 23, w, Align::Center, fitText(FONT_XS, combat_.result, w - 4), theme::dim);
        }
    }

    // Side panel during the agenda phase: the agenda, its outcomes and the votes so far.
    void drawAgendaPanel(Canvas& c, const Game& g) {
        const Rect p{mapW() + 4, kTopH + 4, W() - mapW() - 8, kHintY - kTopH - 8};
        drawPanel(c, p, rgb(14, 20, 40), rgb(80, 120, 210), 8, kSideAlpha + 30);
        int x = p.x + 8, w = p.w - 16, y = p.y + 6;
        int a = g.currentAgenda;
        drawText(c, FONT_MDB, x, y, "AGENDA", rgb(140, 180, 255));
        y += 19;
        if (a < 0) {
            for (const std::string& l : wrapText(FONT_SM, agendaResult_, w)) {
                drawText(c, FONT_SM, x, y, l, theme::text);
                y += 15;
            }
            return;
        }
        int shown = a;
        bool hidden = false;
        if (!std::strcmp(kAgendas[a].key, "covert") && g.covertAgenda >= 0) {
            if (g.speaker == session_->human()) shown = g.covertAgenda;
            else hidden = true;
        }
        for (const std::string& l : wrapText(FONT_SMB, kAgendas[shown].name, w)) {
            drawText(c, FONT_SMB, x, y, l, theme::text);
            y += 15;
        }
        drawText(c, FONT_XS, x, y, kAgendas[shown].law ? "Law" : "Directive", theme::dim);
        y += 15;
        auto para = [&](const std::string& head, const std::string& text, uint32_t col) {
            int n = 0;
            for (const std::string& l : wrapText(FONT_XS, head + text, w)) {
                if (n++ >= 5) break;
                drawText(c, FONT_XS, x, y, l, col);
                y += 12;
            }
            y += 3;
        };
        if (hidden) {
            para("", "Covert Legislation: only the speaker knows what is being voted on.", theme::gold);
        } else {
            bool fa = kAgendas[shown].elect == Elect::ForAgainst;
            para(fa ? "For: " : "", agendaText(shown, false), rgb(200, 210, 230));
            if (fa) para("Against: ", agendaText(shown, true), rgb(200, 210, 230));
        }
        c.hline(x, x + w, y, rgb(60, 80, 130));
        y += 5;
        // votes so far
        std::vector<std::pair<int, int>> tally;  // outcome, votes
        for (int q = 0; q < g.numPlayers && q < int(g.votedFor.size()); ++q) {
            if (g.votedFor[q] == kNoVote) continue;
            bool found = false;
            for (auto& t : tally)
                if (t.first == g.votedFor[q]) {
                    t.second += g.voteCount[q];
                    found = true;
                }
            if (!found) tally.push_back({g.votedFor[q], g.voteCount[q]});
        }
        drawText(c, FONT_SMB, x, y, "Votes", theme::gold);
        y += 16;
        if (tally.empty()) {
            drawText(c, FONT_XS, x, y, "None yet", theme::faint);
            y += 13;
        }
        int elect = int(kAgendas[shown].elect);
        auto outcomeLabel = [&](int o) -> std::string {
            if (hidden && kAgendas[g.covertAgenda].elect == Elect::ForAgainst) return o == 0 ? "For" : "Against";
            switch (Elect(elect)) {
                case Elect::ForAgainst: return o == 0 ? "For" : "Against";
                case Elect::Player: return playerName(g, o);
                case Elect::Planet:
                case Elect::NonHomePlanet: return kPlanets[g.planets[o].def].name;
                case Elect::Law: return kAgendas[o].name;
                case Elect::ScoredSecret: return kObjectives[o].name;
                case Elect::StrategyCard: return strategyName(o);
                default: return std::to_string(o);
            }
        };
        for (auto& t : tally) {
            drawText(c, FONT_SM, x + 4, y, fitText(FONT_SM, outcomeLabel(t.first), w - 50), theme::text);
            drawTextAligned(c, FONT_SMB, x, y, w, Align::Right, std::to_string(t.second), theme::gold);
            y += 15;
        }
        y += 4;
        for (int q = 0; q < g.numPlayers && q < int(g.votedFor.size()); ++q) {
            if (y > p.y + p.h - 30) break;
            if (g.votedFor[q] == kNoVote) continue;
            drawText(c, FONT_XS, x, y,
                     fitText(FONT_XS, playerName(g, q) + ": " + std::to_string(g.voteCount[q]) + " for " + outcomeLabel(g.votedFor[q]), w),
                     pcolor(g, q));
            y += 12;
        }
        if (!agendaResult_.empty()) {
            int ry = p.y + p.h - 48;
            c.fillRoundRect(x - 2, ry, w + 4, 44, 6, rgb(30, 45, 80));
            auto lines = wrapText(FONT_XS, agendaResult_, w - 6);
            int yy = ry + 3;
            for (size_t i = 0; i < lines.size() && i < 3; ++i) {
                drawText(c, FONT_XS, x + 2, yy, lines[i], theme::text);
                yy += 13;
            }
        }
    }

    void drawFeed(Canvas& c, uint32_t now, bool withDecision) {
        const auto& feed = session_->feed();
        if (feed.empty()) return;
        const Game& g = session_->game();
        int n = std::min<int>(withDecision ? 3 : 4, int(feed.size()));
        int h = n * 14 + 6;
        int y0 = kHintY - h - 2;
        c.fillRect(0, y0, mapW(), h + 2, rgb(4, 6, 12), 175);
        int y = y0 + 3;
        for (int i = int(feed.size()) - n; i < int(feed.size()); ++i) {
            const FeedLine& f = feed[i];
            uint32_t col = f.player >= 0 ? pcolor(g, f.player) : theme::gold;
            if (f.kind == EventKind::Dice) col = rgb(200, 205, 220);
            if (f.kind == EventKind::Warning) col = rgb(255, 200, 120);
            int age = int(now - f.time);
            int alpha = (i == int(feed.size()) - 1 && age < 400) ? 255 : 215;
            drawText(c, FONT_XS, 6, y, fitText(FONT_XS, f.text, mapW() - 12), col, alpha);
            y += 14;
        }
    }

    // ------------------------------------------------------------- overlays
    void updateOverlay(App& app, const Input& in) {
        const Game& g = session_->game();
        switch (overlay_) {
            case Overlay::Menu: {
                if (menuWeb_) {
                    updateMenuWeb(in);
                    break;
                }
                const int n = 18;
                if (in.pressed(Btn::Up)) menuSel_ = (menuSel_ + n - 1) % n;
                if (in.pressed(Btn::Down)) menuSel_ = (menuSel_ + 1) % n;
                if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) overlay_ = Overlay::None;
                if (menuSel_ == 11 && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
                    int sp = (speed_ + (in.pressed(Btn::Left) ? 3 : 1)) % 4;
                    app.settings().setInt("ai_speed", sp);
                    app.settings().save();
                    speed_ = sp;
                    return;
                }
                if (menuSel_ == 12 && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
                    int m = (cardPause_ + (in.pressed(Btn::Left) ? 2 : 1)) % 3;
                    app.settings().setInt("card_pause", m);
                    app.settings().save();
                    cardPause_ = m;
                    session_->setCardPause(m);
                    return;
                }
                if (menuSel_ == 13 && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
                    const bool on = !map_.borders();
                    app.settings().setInt("map_borders", on ? 1 : 0);
                    app.settings().save();
                    map_.setBorders(on);
                    return;
                }
                if (menuSel_ == 14 && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
                    const bool on = app.settings().getInt("info_tiles", 1) == 0;
                    app.settings().setInt("info_tiles", on ? 1 : 0);
                    app.settings().save();
                    map_.setInfoTiles(on);
                    return;
                }
                if ((menuSel_ == 15 || menuSel_ == 16) && (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::A))) {
                    const bool music = menuSel_ == 15;
                    int v = music ? audio::musicVolume() : audio::soundVolume();
                    v = in.pressed(Btn::Left) ? std::max(0, v - 1) : in.pressed(Btn::Right) ? std::min(10, v + 1) : (v + 1) % 11;
                    if (music) audio::setMusicVolume(v);
                    else audio::setSoundVolume(v);
                    app.settings().setInt(music ? "music_vol" : "sound_vol", v);
                    app.settings().save();
                    if (!in.pressed(Btn::A)) audio::play(audio::Sfx::Toggle);
                    return;
                }
                if (in.pressed(Btn::A)) {
                    int h = session_->seat();
                    fromMenu_ = menuSel_ >= 2 && menuSel_ <= 10;  // the screens opened from here come back here
                    switch (menuSel_) {
                        case 0: overlay_ = Overlay::None; break;
                        case 1:
                            menuWeb_ = true;
                            if (webSel_ < 0 || webSel_ >= g.numPlayers || webSel_ == h) webSel_ = firstWebPick();
                            break;
                        case 2: overlay_ = Overlay::Objectives; break;
                        case 3:
                            overlay_ = Overlay::None;
                            info_ = makeLawScreen(g, h);
                            break;
                        case 4:
                            overlay_ = Overlay::None;
                            info_ = makeHandScreen(g, h);
                            break;
                        case 5:
                            overlay_ = Overlay::None;
                            info_ = makeFactionSheet(g, h);
                            break;
                        case 6:
                            overlay_ = Overlay::None;
                            info_ = makeUnitScreen(g, h);
                            break;
                        case 7:
                            overlay_ = Overlay::None;
                            info_ = makeTechScreen(g, h);
                            break;
                        case 8:
                            overlay_ = Overlay::None;
                            info_ = makePlanetScreen(g, h);
                            break;
                        case 9: overlay_ = Overlay::Players; break;
                        case 10:
                            overlay_ = Overlay::Log;
                            logScroll_ = 0;
                            break;
                        case 17:
                            session_->save();
                            habits::record(app.settings(), session_->game(), session_->engine().setup());  // (0.54.0; counted again later replaces it)
                            app.pop();
                            break;
                    }
                }
                break;
            }
            case Overlay::Log: {
                int total = int(session_->game().log.size());
                if (in.pressed(Btn::Up)) logScroll_ = std::min(total, logScroll_ + 1);
                if (in.pressed(Btn::Down)) logScroll_ = std::max(0, logScroll_ - 1);
                if (in.pressed(Btn::L1) || in.pressed(Btn::Left)) logScroll_ = std::min(total, logScroll_ + 20);
                if (in.pressed(Btn::R1) || in.pressed(Btn::Right)) logScroll_ = std::max(0, logScroll_ - 20);
                if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Y) || in.pressed(Btn::Menu)) overlay_ = Overlay::None;
                break;
            }
            case Overlay::GameOver:
                if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
                    session_->deleteSave();
                    app.pop();
                }
                break;
            case Overlay::Objectives:
                updateObjectives(g, in);
                // (A doesn't close it: the chosen card is already shown in full below)
                if (in.pressed(Btn::B) || in.pressed(Btn::Menu) || in.pressed(Btn::X)) overlay_ = Overlay::None;
                break;
            default:
                if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Menu) || in.pressed(Btn::Select) ||
                    in.pressed(Btn::X))
                    overlay_ = Overlay::None;
                break;
        }
    }

    void drawOverlay(App&, Canvas& c, const Game& g) {
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        switch (overlay_) {
            case Overlay::Menu: drawMenuOverlay(c, g, lastNow_); break;
            case Overlay::Players: drawPlayersOverlay(c, g); break;
            case Overlay::Objectives: drawObjectivesOverlay(c, g); break;
            case Overlay::Log: drawLogOverlay(c, g); break;
            case Overlay::GameOver: drawGameOver(c, g); break;
            default: break;
        }
    }

    // ------------------------------------------------------------- menu + negotiations web
    std::vector<bool> webAllowed() const {
        const Game& g = session_->game();
        std::vector<bool> v(size_t(g.numPlayers), false);
        for (int q = 0; q < g.numPlayers; ++q) v[size_t(q)] = q != session_->seat() && !g.players[q].eliminated;
        return v;
    }
    int firstWebPick() const {
        const Game& g = session_->game();
        FactionWeb w = factionWebLayout(g, kWebArea, session_->seat());
        std::vector<bool> ok = webAllowed();
        int best = -1;
        float bd = 1e9f;
        const int me = session_->seat();
        for (int q = 0; q < g.numPlayers; ++q)
            if (ok[size_t(q)]) {
                float d = std::fabs(w.x[q] - w.cx) + std::fabs(w.y[q] - (w.cy + 250)) +
                          (me >= 0 && transactionAllowed(g, me, q) ? 0.f : me >= 0 && areNeighbours(g, me, q) ? 500.f : 2000.f);
                if (d < bd) {
                    bd = d;
                    best = q;
                }
            }
        return best;
    }
    // Can the player start an offer to q from the menu right now? (why not, if not)
    bool offerFromMenu(int q, std::string* why) const {
        const Game& g = session_->game();
        const int me = session_->human();
        auto no = [&](const std::string& w) {
            if (why) *why = w;
            return false;
        };
        if (me < 0) return no("You're watching this game.");
        if (q < 0 || q == me) return no("");
        if (!session_->humanToAnswer()) return no("Wait for your turn: the other players are acting.");
        const Decision& d = session_->engine().decision();
        if (d.player != me) return no("Wait for your turn.");
        if (d.tag == Tag::Negotiate) {
            for (const Option& o : d.options)
                if (o.id == q && o.enabled) return true;
            return no("No more offers to " + playerName(g, q) + " this " + (g.phase == Phase::Agenda ? "agenda." : "turn."));
        }
        if (d.tag == Tag::ActionChoice) {
            bool neg = false;
            for (const Option& o : d.options)
                if (o.id == 650 && o.enabled) neg = true;
            if (neg && canOffer(g, me, q)) return true;
            if (neg) return no("No more offers to " + playerName(g, q) + " this turn.");
        }
        return no("You can make offers when choosing your action, or before a vote.");
    }
    void updateMenuWeb(const Input& in) {
        const Game& g = session_->game();
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            menuWeb_ = false;
            return;
        }
        int dx = 0, dy = 0;
        if (in.pressed(Btn::Left)) dx = -1;
        if (in.pressed(Btn::Right)) dx = 1;
        if (in.pressed(Btn::Up)) dy = -1;
        if (in.pressed(Btn::Down)) dy = 1;
        if (dx || dy) webSel_ = webStep(factionWebLayout(g, kWebArea, session_->seat()), webSel_, dx, dy, webAllowed());
        if (in.pressed(Btn::A)) {
            std::string why;
            if (!offerFromMenu(webSel_, &why)) {
                webMsg_ = why;
                return;
            }
            const Decision& d = session_->engine().decision();
            Answer a;
            std::string err;
            if (d.tag == Tag::Negotiate) {
                a.picks = {webSel_};
            } else {
                a.picks = {650};
                pendingPartner_ = webSel_;
            }
            if (session_->submit(a, &err, lastNow_)) {
                ui_.reset();
                overlay_ = Overlay::None;
                menuWeb_ = false;
            } else {
                pendingPartner_ = -1;
                webMsg_ = err;
            }
        }
    }

    void drawMenuOverlay(Canvas& c, const Game& g, uint32_t now) {
        // the negotiations web takes the map's place; the side panel and hint bar are covered too
        c.fillRect(0, kTopH, 640, 480 - kTopH, rgb(5, 8, 16));
        c.vline(kMapW + 1, kTopH, kHintY - 1, theme::border);
        drawText(c, FONT_LG, 12, kTopH + 6, "Negotiations", menuWeb_ ? theme::gold : theme::dim);
        drawTextAligned(c, FONT_XS, 0, kTopH + 14, kMapW - 12, Align::Right,
                        menuWeb_ ? "choose a faction" : "Negotiations (right) to choose a faction", theme::faint);
        FactionWeb w = factionWebLayout(g, kWebArea, session_->seat());
        std::vector<bool> ok = webAllowed();
        drawFactionWeb(c, g, w, session_->seat(), menuWeb_ ? webSel_ : -1, now, &ok);
        for (int q = 0; q < w.n; ++q)  // the mouse (0.28.0): point at a faction, click to make an offer
            if (q != session_->seat() && q < int(ok.size()) && ok[size_t(q)]) {
                const Rect r{int(w.x[q]) - 26, int(w.y[q]) - 26, 52, 52};
                // 0.32.1 (owner): passing over the web while the menu list has the focus moved the focus to
                // Negotiations - now only a click does that; once there, pointing picks and a click offers
                if (menuWeb_) hits::add(r, [this, q]() { webSel_ = q; });
                else hits::addAction(r, [this, q]() {
                    menuWeb_ = true;
                    webSel_ = q;
                    webMsg_.clear();
                });
            }
        drawWebLegend(c, 12, kHintY - 20);
        const Rect right{kMapW + 6, kTopH + 2, 640 - kMapW - 10, kHintY - kTopH - 6};
        if (menuWeb_) {
            Rect pr = right;
            pr.h -= 62;
            drawRelationsPanel(c, g, pr, session_->seat(), webSel_);
            std::string why;
            bool can = offerFromMenu(webSel_, &why);
            Rect b{right.x, right.y + right.h - 56, right.w, 56};
            drawPanel(c, b, can ? rgb(20, 44, 30) : theme::panel, can ? theme::green : theme::border, 8);
            drawTextAligned(c, FONT_MDB, b.x, b.y + 6, b.w, Align::Center, "Make an offer", can ? theme::text : theme::faint);
            std::string sub = !webMsg_.empty() ? webMsg_ : can ? keyText("Press A") : why;
            auto lines = wrapText(FONT_XS, sub, b.w - 12);
            int yy = b.y + 26;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) {
                drawTextAligned(c, FONT_XS, b.x, yy, b.w, Align::Center, lines[k], webMsg_.empty() ? theme::dim : rgb(255, 200, 150));
                yy += 12;
            }
            drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Make an offer"}, {"B", "Back"}});
            return;
        }
        drawPanel(c, right, theme::panel, theme::gold, 10);
        drawTextAligned(c, FONT_MDB, right.x, right.y + 6, right.w, Align::Center, "Game menu", theme::gold);
        std::string speed = std::string("AI speed: ") + kSpeedNames[std::max(0, std::min(3, speed_))];
        // (0.26.0, owner: Negotiations, Objectives, Laws, Your cards, Faction sheet, Units,
        // Technology, Planets, Players from the top; the cases in updateOverlay follow)
        std::vector<MenuItem> items = {{"Resume", "Back to the game"},
                                       {"Negotiations", "Choose a faction on the web: deals, promises, offers"},
                                       {"Objectives", "Public and secret objectives"},
                                       {"Laws", "Laws in play and every agenda"},
                                       {"Your cards", "Action cards and promissory notes in your hand"},
                                       {"Faction sheet", "Command tokens, trade goods, leaders, abilities"},
                                       {"Units", "Unit cards: costs, stats and abilities"},
                                       breakthroughsOn(g) ? MenuItem{"Technology & Breakthroughs", "Your technology and breakthrough cards, and what you can research"}
                                                          : MenuItem{"Technology", "Your technology cards and what you can research"},
                                       {"Planets", "Your planet cards"},
                                       {"Players", "Everyone's victory points, cards and tokens"},
                                       {"Game log", "Everything that has happened"},
                                       {speed, "\xE2\x97\x80 \xE2\x96\xB6 how fast the computer plays"},
                                       {keyText(std::string("Cards: ") + kCardPauseNames[std::max(0, std::min(2, cardPause_))]),
                                        keyText("\xE2\x97\x80 \xE2\x96\xB6 cards and the end of a space combat wait for A, or show briefly")},
                                       {std::string("Borders: ") + (map_.borders() ? "On" : "Off"),
                                        "\xE2\x97\x80 \xE2\x96\xB6 coloured borders around each player's territory on the map"},
                                       {std::string("Info tiles: ") + (map_.infoTiles() ? "On" : "Off"),
                                        "\xE2\x97\x80 \xE2\x96\xB6 each faction's tokens, trade goods, cards and secrets beside its home"},
                                       // 0.27.0: sound
                                       {volumeLabel("Music", audio::musicVolume()), "\xE2\x97\x80 \xE2\x96\xB6 music volume (0 = off)"},
                                       {volumeLabel("Sounds", audio::soundVolume()), "\xE2\x97\x80 \xE2\x96\xB6 sound effects volume (0 = off)"},
                                       {"Save and quit", "Continue later from the title screen"}};
        int y = right.y + 28;
        for (size_t i = 0; i < items.size(); ++i) {
            bool sel = int(i) == menuSel_;
            hits::add(Rect{right.x + 6, y - 2, right.w - 12, 19}, [this, i]() { menuSel_ = int(i); });  // the mouse (0.27.0)
            if (sel) c.fillRoundRect(right.x + 6, y - 2, right.w - 12, 19, 6, theme::panelHi);
            drawText(c, FONT_SMB, right.x + 14, y, fitText(FONT_SMB, items[i].label, right.w - 24), sel ? theme::text : theme::dim);
            y += 20;
        }
        auto lines = wrapText(FONT_XS, items[size_t(menuSel_)].detail, right.w - 16);
        int yy = right.y + right.h - 30;
        for (size_t k = 0; k < lines.size() && k < 2; ++k) {
            drawTextAligned(c, FONT_XS, right.x, yy, right.w, Align::Center, lines[k], theme::faint);
            yy += 12;
        }
        drawHintBar(c, {{"A", "Select"}, {"B", "Back"}});
    }

    void drawPlayersOverlay(Canvas& c, const Game& g) {
        Rect r{10, 10, 620, 432};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        drawText(c, FONT_LG, r.x + 14, r.y + 10, "Players", theme::gold);
        drawTextAligned(c, FONT_XS, r.x, r.y + 16, r.w - 14, Align::Right, "in turn order \xC2\xB7 \xE2\x98\x85 = speaker", theme::faint);
        const char* heads[] = {"Faction", "VP", "Strategy", "Tokens T/F/S", "TG", "Comm", "Action", "Planets", "Res/Inf", "Techs"};
        int cols[] = {r.x + 14, r.x + 168, r.x + 198, r.x + 262, r.x + 342, r.x + 372, r.x + 412, r.x + 455, r.x + 510, r.x + 575};
        int y = r.y + 44;
        for (int i = 0; i < 10; ++i) {
            const char* ic = i == 4 ? "trade_good" : i == 5 ? "commodity" : i == 6 ? "action_card" : nullptr;
            if (ic && icons::draw(c, ic, cols[i], y - 2, 14)) continue;
            drawText(c, FONT_XS, cols[i], y, heads[i], theme::dim);
        }
        y += 18;
        for (int q : initiativeOrder(g)) {
            const Player& pl = g.players[q];
            c.fillRect(r.x + 6, y + 2, 4, 30, pcolor(g, q));
            const int fx = icons::available() ? 30 : 0;
            if (fx) drawFactionIcon(c, pl.faction, cols[0], y + 1, 26);
            std::string name = playerName(g, q) + (q == session_->human() ? " (you)" : "") + (q == g.speaker ? " \xE2\x98\x85" : "");
            drawText(c, FONT_SMB, cols[0] + fx, y, fitText(FONT_SMB, name, 150 - fx), pcolor(g, q));
            std::string sub = kFactions[pl.faction].name;
            if (archetypesRevealed(g) && isBot(g, q)) sub = archName(g, q) + " \xC2\xB7 " + sub;  // after round 3
            drawText(c, FONT_XS, cols[0] + fx, y + 16, fitText(FONT_XS, sub, 150 - fx), archetypesRevealed(g) && isBot(g, q) ? rgb(230, 190, 120) : theme::faint);
            drawText(c, FONT_SMB, cols[1], y + 6, std::to_string(pl.vp), theme::gold);
            std::string cards;
            for (int8_t cd : pl.cards) cards += std::string(cards.empty() ? "" : ",") + std::to_string(cd + 1);
            drawText(c, FONT_SM, cols[2], y + 6, cards.empty() ? "-" : cards + (pl.passed ? " (p)" : ""), theme::text);
            drawText(c, FONT_SM, cols[3], y + 6,
                     std::to_string(pl.tactic) + "/" + std::to_string(pl.fleet) + "/" + std::to_string(pl.strategy), theme::text);
            drawText(c, FONT_SM, cols[4], y + 6, std::to_string(pl.tradeGoods), theme::text);
            drawText(c, FONT_SM, cols[5], y + 6, std::to_string(pl.commodities), theme::text);
            drawText(c, FONT_SM, cols[6], y + 6, std::to_string(pl.actionCards.size()), theme::text);
            drawText(c, FONT_SM, cols[7], y + 6, std::to_string(controlledPlanets(g, q).size()), theme::text);
            drawText(c, FONT_SM, cols[8], y + 6,
                     std::to_string(spendable(g, q, false, false)) + "/" + std::to_string(spendable(g, q, true, false)), theme::text);
            drawText(c, FONT_SM, cols[9], y + 6, std::to_string(pl.techs.size()), theme::text);
            y += std::min(46, 360 / std::max(1, g.numPlayers));
        }
        drawHintBar(c, {{"B", "Back"}});
    }

    // The objectives board: stage I and stage II rows (face down until
    // revealed), your secrets held and scored, and the chosen card in full.
    struct ObjSlot {
        int objective = -1;  // -1 = face down / empty
        int kind = 1;        // 1 stage I, 2 stage II, 3 secret
        bool scoredSecret = false;
        int special = 0;     // 0.43.0: 1 Oracle AI (only you see it), 2 a Tnelis contract (Turra Sveyar), 3 another's scored secret
        int owner = -1;      // ... whose
    };
    // 0.43.0: the secrets other players have scored (they are public) - the secrets block shows them 6 at a time (Y)
    std::vector<std::pair<int, int>> othersSecrets(const Game& g) const {
        std::vector<std::pair<int, int>> v;  // (objective, player)
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == session_->seat()) continue;
            for (int16_t o : g.players[size_t(q)].scored)
                if (kObjectives[o].kind == 3) v.push_back({o, q});
        }
        return v;
    }
    std::vector<ObjSlot> objectiveRow(const Game& g, int row) const {
        std::vector<ObjSlot> v;
        const int h = session_->seat();
        if (row < 2) {
            const int kind = row + 1;
            for (int16_t o : g.publicObjectives)
                if (kObjectives[o].kind == kind || (kind == 2 && kObjectives[o].kind == 3)) v.push_back({o, kObjectives[o].kind, false});
            int shown = 0;
            for (const ObjSlot& s : v) shown += s.kind == kind ? 1 : 0;
            for (int k = shown; k < 5 && int(v.size()) < 6; ++k) v.push_back({-1, kind, false});
            // Oracle AI (Augurs, 0.43.0): the next objective of this stage, face up for its player (and Read the Fates)
            for (int q = 0; q < g.numPlayers; ++q) {
                const int o = g.players[size_t(q)].dsSheet;
                if (o < 0 || kObjectives[o].kind != kind || !(g.modules & kModDiscordant) || !isFaction(g, q, "augers")) continue;
                bool sees = h == q;
                if (h >= 0 && h < g.numPlayers) {
                    const auto& nip = g.players[size_t(h)].notesInPlay;
                    sees = sees || std::find(nip.begin(), nip.end(), int16_t(makeNote(q, kNoteFaction))) != nip.end();
                }
                if (!sees) continue;
                for (ObjSlot& s : v)
                    if (s.objective < 0 && s.kind == kind) {
                        s.objective = o, s.special = 1, s.owner = q;
                        break;
                    }
            }
            // Turra Sveyar (Tnelis hero, 0.43.0): a secret bought as a contract, scored like a public objective
            if (row == 1)
                for (int q = 0; q < g.numPlayers; ++q) {
                    const int o = g.players[size_t(q)].dsPubSecret;
                    if (o < 0 || o >= kObjectiveCount || !(g.modules & kModDiscordant)) continue;
                    ObjSlot s{o, 3, false, 2, q};
                    if (int(v.size()) < 6) v.push_back(s);
                    else
                        for (ObjSlot& t : v)
                            if (t.objective < 0) {
                                t = s;
                                break;
                            }
                }
        } else if (othersView_) {  // the secrets other players have scored, a page of 6
            const auto all = othersSecrets(g);
            for (int k = 0; k < 3; ++k) {
                const size_t i = size_t(objPage_ * 6 + (row - 2) * 3 + k);
                if (i < all.size()) v.push_back({all[i].first, 3, true, 3, all[i].second});
                else v.push_back({-1, 3, true});
            }
        } else if (h >= 0) {
            const Player& pl = g.players[h];
            if (row == 2) {
                for (int16_t o : pl.secrets) v.push_back({o, 3, false});
            } else {
                for (int16_t o : pl.scored)
                    if (kObjectives[o].kind == 3) v.push_back({o, 3, true});
            }
            while (v.size() < 3) v.push_back({-1, 3, row == 3});
        }
        if (v.size() > 6) v.resize(6);
        return v;
    }
    // Board positions: columns 0-5 publics (rows 0 / 1), 6-8 secrets (held / scored).
    ObjSlot objectiveAt(const Game& g, int col, int row) const {
        auto v = objectiveRow(g, col < 6 ? row : row + 2);
        int k = col < 6 ? col : col - 6;
        return k < int(v.size()) ? v[k] : ObjSlot{-1, col < 6 ? row + 1 : 3, col >= 6 && row == 1};
    }
    void updateObjectives(const Game& g, const Input& in) {
        auto width = [&](int row, bool secrets) { return int(objectiveRow(g, secrets ? row + 2 : row).size()); };
        if (in.pressed(Btn::Y)) {  // 0.43.0: your secrets <-> the secrets the others have scored
            othersView_ = !othersView_;
            objPage_ = 0;
        }
        if (othersView_) {
            const int pages = std::max(1, (int(othersSecrets(g).size()) + 5) / 6);
            if (in.pressed(Btn::L1)) objPage_ = (objPage_ + pages - 1) % pages;
            if (in.pressed(Btn::R1)) objPage_ = (objPage_ + 1) % pages;
        }
        int& col = objCol_;
        int& row = objRow_;
        const bool sec = col >= 6;
        if (in.pressed(Btn::Up) || in.pressed(Btn::Down)) row = 1 - row;
        if (in.pressed(Btn::Right)) {
            if (!sec && col + 1 < width(row, false)) ++col;
            else if (!sec && (session_->seat() >= 0 || othersView_)) col = 6;
            else if (sec && col - 6 + 1 < 3) ++col;
        }
        if (in.pressed(Btn::Left)) {
            if (sec && col > 6) --col;
            else if (sec) col = width(row, false) - 1;
            else if (col > 0) --col;
        }
        // keep the cursor on a card of its row - judged by where it is now (the old
        // test used the column from before the move, which pulled the cursor straight
        // back out of the secrets: they could be seen but never chosen)
        if (col < 6 && col >= width(row, false)) col = std::max(0, width(row, false) - 1);
        if (col >= 6 && ((session_->seat() < 0 && !othersView_) || col > 8)) col = std::max(0, width(row, false) - 1);
    }

    void drawObjectivesOverlay(Canvas& c, const Game& g) {
        Rect r{10, 10, 620, 432};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        drawText(c, FONT_LG, r.x + 14, r.y + 8, "Objectives", theme::gold);
        drawTextAligned(c, FONT_SM, r.x, r.y + 12, r.w - 14, Align::Right,
                        "First to " + std::to_string(g.vpGoal) + " victory points wins", theme::dim);
        const int h = session_->seat();
        const int CW = 60, CH = 90, gap = 6, x0 = r.x + 14, sx0 = r.x + 418;
        const int rowY[2] = {r.y + 50, r.y + 166};
        static const char* rowName[2] = {"STAGE I", "STAGE II"};
        const uint32_t pulse = mix(theme::goldDim, theme::gold, int((0.5f + 0.5f * std::sin(float(lastNow_ % 1500) / 1500.f * 6.2831853f)) * 255));
        auto drawSlot = [&](const ObjSlot& s, Rect cr, bool selected) {
            if (s.objective >= 0) cards::objective(c, s.objective, cr);
            else if (s.kind == 3) {
                c.fillRoundRect(cr.x, cr.y, cr.w, cr.h, 6, rgb(16, 18, 30));
                c.frameRoundRect(cr.x, cr.y, cr.w, cr.h, 6, theme::border, 1);
            } else cards::objectiveBack(c, s.kind, cr);
            if (s.objective >= 0 && !s.scoredSecret && s.kind != 3) {  // who scored it: a dot each
                int n = 0;
                for (int q = 0; q < g.numPlayers; ++q) n += hasScored(g, q, s.objective) ? 1 : 0;
                int dx = cr.x + cr.w / 2 - n * 4 + 4;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (hasScored(g, q, s.objective)) {
                        c.fillCircle(float(dx), float(cr.y + cr.h + 6), 3.5f, pcolor(g, q));
                        dx += 8;
                    }
            }
            if (s.scoredSecret && s.objective >= 0) {
                c.fillRoundRect(cr.x + 4, cr.y + cr.h - 18, cr.w - 8, 14, 5, rgb(30, 110, 60));
                drawTextAligned(c, FONT_XS, cr.x, cr.y + cr.h - 17, cr.w, Align::Center, "SCORED", theme::text);
            }
            if (h >= 0 && s.objective >= 0 && !s.scoredSecret && !hasScored(g, h, s.objective) && meetsObjective(g, h, s.objective)) {
                c.fillRoundRect(cr.x + 4, cr.y + 3, cr.w - 8, 13, 5, rgb(20, 110, 130));
                drawTextAligned(c, FONT_XS, cr.x, cr.y + 3, cr.w, Align::Center, "READY", theme::text);
            }
            if (s.special == 1) {  // Oracle AI: a glowing aura - only you see this card
                const float k = 0.5f + 0.5f * std::sin(float(lastNow_ % 1600u) / 1600.f * 6.2831853f);
                for (int i = 4; i >= 1; --i)
                    c.frameRoundRect(cr.x - i, cr.y - i, cr.w + 2 * i, cr.h + 2 * i, 6 + i, rgb(120, 220, 255), 1, int(255 * (0.3f + 0.4f * k) / float(i)));
                c.fillRoundRect(cr.x + 3, cr.y + cr.h - 17, cr.w - 6, 13, 5, rgb(20, 90, 130));
                drawTextAligned(c, FONT_XS, cr.x, cr.y + cr.h - 16, cr.w, Align::Center, "ORACLE", theme::text);
            } else if (s.special == 2) {  // a Tnelis contract
                c.fillRoundRect(cr.x + 3, cr.y + cr.h - 17, cr.w - 6, 13, 5, pcolor(g, s.owner));
                drawTextAligned(c, FONT_XS, cr.x, cr.y + cr.h - 16, cr.w, Align::Center, "CONTRACT", theme::text);
            } else if (s.special == 3 && s.owner >= 0) {  // whose scored secret
                drawFactionIcon(c, g.players[size_t(s.owner)].faction, cr.x + cr.w - 17, cr.y + 3, 14);
                c.fillRect(cr.x + 4, cr.y + cr.h - 2, cr.w - 8, 2, pcolor(g, s.owner));
            }
            if (selected) c.frameRoundRect(cr.x - 3, cr.y - 3, cr.w + 6, cr.h + 6, 7, pulse, 2);
        };
        for (int row = 0; row < 2; ++row) {
            drawText(c, FONT_XS, x0, rowY[row] - 14, rowName[row], row == 0 ? rgb(240, 160, 70) : rgb(110, 170, 255));
            auto v = objectiveRow(g, row);
            for (size_t k = 0; k < v.size(); ++k)
            {
                hits::add(Rect{x0 + int(k) * (CW + gap), rowY[row], CW, CH}, [this, k, row]() {  // the mouse (0.28.0)
                    objCol_ = int(k);
                    objRow_ = row;
                });
                drawSlot(v[k], Rect{x0 + int(k) * (CW + gap), rowY[row], CW, CH}, objCol_ == int(k) && objRow_ == row);
            }
        }
        if (h >= 0 || othersView_) {
            static const char* secName[2] = {"YOUR SECRETS", "SECRETS SCORED"};
            const int pages = std::max(1, (int(othersSecrets(g).size()) + 5) / 6);
            for (int row = 0; row < 2; ++row) {
                if (othersView_)
                    drawText(c, FONT_XS, sx0, rowY[row] - 14,
                             row == 0 ? "SCORED BY OTHERS" + (pages > 1 ? " " + std::to_string(objPage_ + 1) + "/" + std::to_string(pages) : std::string()) : "",
                             rgb(240, 110, 100));
                else drawText(c, FONT_XS, sx0, rowY[row] - 14, secName[row], rgb(240, 110, 100));
                auto v = objectiveRow(g, row + 2);
                for (size_t k = 0; k < v.size() && k < 3; ++k)
                {
                    hits::add(Rect{sx0 + int(k) * (CW + gap), rowY[row], CW, CH}, [this, k, row]() {
                        objCol_ = 6 + int(k);
                        objRow_ = row;
                    });
                    drawSlot(v[k], Rect{sx0 + int(k) * (CW + gap), rowY[row], CW, CH}, objCol_ == 6 + int(k) && objRow_ == row);
                }
            }
        }
        // the chosen card, large, and what it means for you
        const int dy = r.y + 276;
        c.hline(r.x + 12, r.x + r.w - 12, dy - 6, theme::border);
        const ObjSlot s = objectiveAt(g, objCol_, objRow_);
        Rect big{x0, dy, 100, 150};
        int tx = x0 + 114, tw = r.x + r.w - 14 - tx, y = dy + 2;
        if (s.objective >= 0) {
            const ObjectiveDef& d = kObjectives[s.objective];
            cards::objective(c, s.objective, big);
            drawText(c, FONT_MDB, tx, y, fitText(FONT_MDB, d.name, tw), theme::text);
            y += 22;
            std::string kind = d.kind == 1 ? "Stage I public objective" : d.kind == 2 ? "Stage II public objective" : "Secret objective";
            kind += " \xC2\xB7 " + std::to_string(d.points) + (d.points == 1 ? " victory point" : " victory points");
            kind += d.phase == ObjPhase::Action ? " \xC2\xB7 scores the moment it happens" : " \xC2\xB7 scored in the status phase";
            drawText(c, FONT_XS, tx, y, fitText(FONT_XS, kind, tw), d.kind == 1 ? rgb(240, 160, 70) : d.kind == 2 ? rgb(110, 170, 255) : rgb(240, 110, 100));
            y += 17;
            for (const std::string& l : wrapText(FONT_SM, objectiveSummary(s.objective), tw)) {
                if (y > dy + 92) break;
                drawText(c, FONT_SM, tx, y, l, theme::text);
                y += 16;
            }
            y = std::max(y + 4, dy + 100);
            if (s.special == 1) {
                drawText(c, FONT_XS, tx, y, fitText(FONT_XS, "Oracle AI: the next " + std::string(d.kind == 1 ? "stage I" : "stage II") +
                                                          " objective to be revealed - only you can see it.", tw), rgb(140, 225, 255));
                y += 15;
            } else if (s.special == 2 && s.owner >= 0) {
                drawText(c, FONT_XS, tx, y, fitText(FONT_XS, playerName(g, s.owner) + " holds it as a contract (Turra Sveyar): scored like a public objective.", tw),
                         theme::dim);
                y += 15;
            } else if (s.special == 3 && s.owner >= 0 && h >= 0 && meetsObjective(g, h, s.objective) &&
                       (isFaction(g, h, "firmament") || isFaction(g, h, "obsidian"))) {
                drawText(c, FONT_XS, tx, y, fitText(FONT_XS, "You meet it too: Plots Within Plots can score it for you.", tw), theme::cyan);
                y += 15;
            }
            if (d.kind != 3 || s.scoredSecret) {
                std::string who;
                for (int q = 0; q < g.numPlayers; ++q)
                    if (hasScored(g, q, s.objective)) who += (who.empty() ? "" : ", ") + playerName(g, q);
                drawText(c, FONT_XS, tx, y, fitText(FONT_XS, "Scored by: " + (who.empty() ? std::string("nobody yet") : who), tw), theme::dim);
                y += 15;
            }
            if (h >= 0 && s.special != 3 && s.special != 1) {
                std::string st;
                uint32_t col = theme::dim;
                if (hasScored(g, h, s.objective)) st = "You have scored this.", col = theme::green;
                else if (meetsObjective(g, h, s.objective))
                    st = d.phase == ObjPhase::Action ? "You can score this now." : "You qualify: you can score it in the status phase.", col = theme::cyan;
                else st = "You don't qualify yet.";
                drawText(c, FONT_SMB, tx, y, fitText(FONT_SMB, st, tw), col);
            }
        } else if (s.kind == 3) {
            c.fillRoundRect(big.x, big.y, big.w, big.h, 8, rgb(16, 18, 30));
            c.frameRoundRect(big.x, big.y, big.w, big.h, 8, theme::border, 1);
            drawText(c, FONT_MDB, tx, y, s.scoredSecret ? "No scored secret here" : "No secret objective here", theme::dim);
            drawWrappedLines(c, FONT_SM, tx, y + 24, tw,
                             othersView_ && objCol_ >= 6 ? "Secrets the other players score are shown here (scored secrets are public)."
                                                         : "You can hold up to 3 secret objectives and score up to 3. You draw one in each status phase.",
                             theme::dim);
        } else {
            cards::objectiveBack(c, s.kind, big);
            drawText(c, FONT_MDB, tx, y, s.kind == 1 ? "Stage I - not revealed yet" : "Stage II - not revealed yet", theme::text);
            drawWrappedLines(c, FONT_SM, tx, y + 24, tw,
                             "One more public objective is revealed at the end of each status phase: the five stage I cards first, then the stage II cards (2 victory points each).",
                             theme::dim);
        }
        if (othersView_) drawHintBar(c, {{"D-PAD", "Choose a card"}, {"Y", "Your secrets"}, {"L1/R1", "Page"}, {"B", "Back"}});
        else drawHintBar(c, {{"D-PAD", "Choose a card"}, {"Y", "Others' secrets"}, {"B", "Back"}});
    }
    void drawWrappedLines(Canvas& c, const FontData& f, int x, int y, int w, const std::string& s, uint32_t col) {
        for (const std::string& l : wrapText(f, s, w)) {
            drawText(c, f, x, y, l, col);
            y += f.lineHeight;
        }
    }

    void drawLogOverlay(Canvas& c, const Game& g) {
        Rect r{10, 10, 620, 432};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        drawText(c, FONT_LG, r.x + 14, r.y + 10, "Game log", theme::gold);
        const int lineH = 15, maxLines = (r.h - 50) / lineH;
        int end = int(g.log.size()) - logScroll_;
        int start = std::max(0, end - maxLines);
        int y = r.y + 42;
        for (int i = start; i < end; ++i) {
            const std::string& l = g.log[i];
            uint32_t col = theme::text;
            if (l.rfind("---", 0) == 0) col = theme::gold;
            else if (l.find("victory point") != std::string::npos) col = theme::green;
            else if (l.find(": ") != std::string::npos && l.find(" hit") != std::string::npos) col = theme::dim;
            drawText(c, FONT_XS, r.x + 14, y, fitText(FONT_XS, l, r.w - 28), col);
            y += lineH;
        }
        drawHintBar(c, {{"\xE2\x86\x91\xE2\x86\x93", "Scroll"}, {"L1/R1", "Page"}, {"B", "Back"}});
    }

    void drawGameOver(Canvas& c, const Game& g) {
        Rect r{110, 70, 420, 330};
        drawPanel(c, r, theme::panel, theme::gold, 12);
        int w = g.winner;
        std::string title = w >= 0 ? (w == session_->human() ? "Victory!" : playerName(g, w) + " wins") : "Game over";
        drawTextAligned(c, FONT_XL, r.x, r.y + 14, r.w, Align::Center, title, theme::gold);
        std::vector<int> order;
        for (int q = 0; q < g.numPlayers; ++q) order.push_back(q);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g.players[a].vp > g.players[b].vp; });
        int y = r.y + 70;
        for (int q : order) {
            drawFactionIcon(c, g.players[q].faction, r.x + 14, y - 1, 22);
            drawText(c, FONT_MDB, r.x + 42, y, playerName(g, q) + (q == session_->human() ? " (you)" : ""), pcolor(g, q));
            drawTextAligned(c, FONT_MDB, r.x, y, r.w - 40, Align::Right, std::to_string(g.players[q].vp) + " VP", theme::text);
            y += 26;
        }
        drawTextAligned(c, FONT_SM, r.x, r.y + r.h - 26, r.w, Align::Center,
                        "Round " + std::to_string(g.round) + keyText(" \xC2\xB7 press A to return to the title"), theme::dim);
    }

    // ------------------------------------------------------------- state
    int objCol_ = 0, objRow_ = 0;  // objectives board cursor
    bool othersView_ = false;      // 0.43.0: the secrets block shows the others' scored secrets (Y)
    int objPage_ = 0;
    struct Banner {
        std::string title, sub;
        uint32_t color = 0, start = 0, until = 0;
        bool big = false;
        int faction = -1;  // turn banners: the faction's label (owner's art) slides in / out
        int slide = 0;     // 1 slides in (turn begins), 2 slides out (turn ends / passes)
        uint32_t labelColour = 0;
        std::string caption;
    };
    struct CardBanner {
        int kind = 0;  // 0 strategy card, 1 command token, 2 technology, 3 action card, 4 exploration, 5 relic, 6 leader, 7 faction ability, 8 agenda, 9 deal
                       // (... 21 a promise broken to or by a person: the betrayal codec, 0.57.0)
        int card = -1, player = -1, other = -1;
        std::string title, sub, text;
        uint32_t start = 0, until = 0;
        // 0.57.0: what the speaking faction says (deal banners: the comms strip; betrayals: the two-line exchange)
        int speaker = -1;
        std::string line, reply;
        uint32_t seed = 0;
        bool laid = false;
        speech::Speech sp, sp2;
    };
    struct Roll {
        int player = -1;
        std::string label;
        std::vector<int8_t> dice, need;
        int hits = 0;
        uint32_t time = 0;
    };
    struct Combat {
        bool open = false, space = true, fireOnly = false, ended = false;
        int sys = -1, planet = -1, pa = -1, pb = -1, winner = -1;
        Force a, b;
        std::vector<Roll> rolls;
        std::vector<tileview::CombatShow::Loss> losses;  // for the explosions on the tile
        std::string title, where, result, lastLoss;
        uint32_t closeAt = 0;
    };

    std::unique_ptr<GameSession> session_;
    MapView map_;
    std::unique_ptr<DecisionUI> ui_;
    std::unique_ptr<InfoScreen> info_;
    int uiId_ = -1;
    Overlay overlay_ = Overlay::None;
    bool peek_ = false;
    bool uiFocus_ = false;  // 0.58.4: the decision editor took the input last frame (see bareClickIsA)
    uint32_t lastMouseTick_ = 0;
    int expanded_ = -1;  // the expanded tile on screen (0.20.0), -1 = none
    int menuSel_ = 0, tab_ = 0, speed_ = 1, logScroll_ = 0, cardPause_ = 0;
    bool menuWeb_ = false;         // the menu's focus is on the faction web
    int webSel_ = -1;              // faction chosen on the web
    int pendingPartner_ = -1;      // chosen on the web: answer "Negotiate with whom?" with it
    std::string webMsg_;
    uint32_t lastNow_ = 0;
    uint32_t lastCursorMove_ = 0;
    Banner centre_;
    CardBanner card_;
    Combat combat_;
    battle::SpaceBattle battle_;  // 0.22.0: the space combat played out on the tile
    battle::GroundBattle ground_;  // 0.23.0: the ground combat on the planet's surface
    std::string toast_;
    uint32_t toastColor_ = 0, toastStart_ = 0, toastUntil_ = 0;
    std::string agendaResult_;
    uint32_t agendaResultUntil_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session) {
    return std::unique_ptr<Scene>(new GameScene(std::move(session)));
}
