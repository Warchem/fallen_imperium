// The application shell: main loop, screen stack, settings and diagnostics.
#pragma once
#include <csignal>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/input.h"
#include "core/settings.h"
#include "gfx/canvas.h"
#include "platform/platform.h"

#define TI4_VERSION "0.59.30"
#if defined(_WIN32) || defined(TI4_SDL2)
#define TI4_STAGE "Stage 12: PC edition \xC2\xB7 Pre-Beta"
#else
#define TI4_STAGE "Pre-Beta"  // (0.58.0: the Miyoo build again - same game as the PC edition)
#endif

class App;

class Scene {
public:
    virtual ~Scene() = default;
    virtual void update(App& app, const Input& in, uint32_t now) = 0;
    virtual void draw(App& app, Canvas& c, uint32_t now) = 0;
    virtual bool animating() const { return false; }  // redraw every frame?
    // By default B and MENU leave a screen; the button test needs every key.
    virtual bool wantsAllButtons() const { return false; }
    // 0.58.4 (owner: clicking a tile under the production window built units he didn't want): does a left click on
    // nothing clickable count as A? (Rows, lists and buttons are always clicked as themselves.)
    virtual bool bareClickIsA(float x, float y) const { (void)x; (void)y; return true; }
    // Holding SELECT for 3 s swaps the screen method (rescue for a black screen).
    virtual bool allowsScreenRescue() const { return true; }
    // Keyboard & mouse (0.27.0): WASD moves this screen's map camera (Input::panX/Y);
    // elsewhere WASD works as the D-pad.
    virtual bool wantsPan() const { return false; }
    // ... and only WASD (0.43.0): a screen with a map whose current editor turns values with left / right
    virtual bool keysPan() const { return wantsPan(); }
    // the mouse wheel at (x, y) moves through a list (Up / Down) instead of zooming a map (0.28.2)
    virtual bool wheelScrolls(float x, float y) const { (void)x; (void)y; return !wantsPan(); }
    // The program is closing (window closed, signal): keep anything worth keeping.
    virtual void onQuit() {}
    // Widescreen (0.46.0): true for a screen laid out for the whole width (theme::screenW()).
    // Others are drawn centred at 640 units, their full-width backgrounds and bars
    // stretched to the edges; the mouse stays in whole-screen units (theme::legacyX()).
    virtual bool wide() const { return false; }
};

struct KeyLogEntry {
    Btn btn;
    bool down;
    int raw;
    uint32_t t;
};

class App {
public:
    App(Platform& p, Settings& s);
    int run();

    void push(std::unique_ptr<Scene> s);
    void pop();
    void quit() { quit_ = true; }
    // Holding SELECT for 3 s on any screen asks main() to switch between the
    // fbdev and SDL screen methods (a rescue if the screen stays black).
    bool switchRequested() const { return switch_; }
    void markDirty() { dirty_ = true; }
    // Set by SIGTERM (e.g. OnionOS shutting down) so we exit cleanly.
    void setStopFlag(volatile std::sig_atomic_t* f) { stopFlag_ = f; }

    Platform& platform() { return platform_; }
    Settings& settings() { return settings_; }

    bool flip() const { return flip_; }
    void setFlip(bool f);
    bool rotateOutput() const { return platform_.defaultRotate() != flip_; }

    const std::deque<KeyLogEntry>& keyLog() const { return keyLog_; }
    float fps() const { return fps_; }
    float drawMs() const { return drawMs_; }
    float presentMs() const { return presentMs_; }

private:
    void checkScale();
    void mouseAndPan(const KeyEvent& e, uint32_t now);
    std::vector<Btn> injected_;
    Btn leftBtn_ = Btn::None;
    float dragDist_ = 0, wheelAcc_ = 0;
    bool panHeld_[4] = {}, panAsDpad_[4] = {};
    Platform& platform_;
    Settings& settings_;
    Canvas canvas_;
    Input input_;
    std::vector<std::unique_ptr<Scene>> stack_;
    std::vector<std::unique_ptr<Scene>> graveyard_;  // popped scenes, freed after the frame
    std::deque<KeyLogEntry> keyLog_;
    bool quit_ = false, dirty_ = true, flip_ = false, switch_ = false;
    float fps_ = 0, drawMs_ = 0, presentMs_ = 0;
    volatile std::sig_atomic_t* stopFlag_ = nullptr;
};

// Screens
std::unique_ptr<Scene> makeTitleScene();
std::unique_ptr<Scene> makeNewGameScene();
std::unique_ptr<Scene> makeTestsMenuScene();
std::unique_ptr<Scene> makeOptionsScene();
std::unique_ptr<Scene> makeControlsScene();
std::unique_ptr<Scene> makeComingSoonScene(const std::string& title, const std::string& sub, const std::vector<std::string>& entries);
std::unique_ptr<Scene> makeButtonTestScene();
std::unique_ptr<Scene> makeDisplayTestScene();
std::unique_ptr<Scene> makeSystemInfoScene(App& app);
