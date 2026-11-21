// Platform layer: puts a finished Canvas on the screen and reports buttons.
//
// Backends:
//   fbdev    - writes straight to /dev/fb0 and reads /dev/input/event*.
//              No libraries needed; this is what OnionOS's own tools do.
//   sdl      - SDL 1.2 loaded at runtime (dlopen) from the device's system
//              libraries. Used if fbdev is unavailable or forced in settings.
//   headless - for automated tests on a PC: replays a script of button
//              presses and saves screenshots.
//   win32    - the Windows version: a resizable window, keyboard controls.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/input.h"
#include "gfx/canvas.h"

class Settings;

struct KeyEvent {
    Btn btn;   // Btn::None if the key isn't one we know
    bool down;
    int raw;   // backend-specific key code (shown in the button test)
    float x = 0, y = 0;  // mouse events: where (game units); wheel: x = notches
};
// Mouse and keyboard-camera events (Windows / Steam Deck, 0.27.0), sent as
// Btn::None events with these raw codes.
constexpr int kRawWheelUp = 0x10001, kRawWheelDown = 0x10002;  // (old: whole notches)
constexpr int kRawMouseMove = 0x10003, kRawMouseLeft = 0x10004, kRawMouseRight = 0x10005, kRawWheel = 0x10006;
constexpr int kRawPanUp = 0x10010, kRawPanDown = 0x10011, kRawPanLeft = 0x10012, kRawPanRight = 0x10013;
// (0.72.0) the camera's speed-up key held / let go (Shift); the pointer left the window or the window lost the focus (no
// edge scrolling until it comes back)
constexpr int kRawPanFast = 0x10014, kRawMouseLeave = 0x10007;
// (0.63.0) a digit typed on the keyboard (the top row or the number pad): kRawDigit0 + 0..9, key down only - for number
// fields (the galaxy seed); the keys do nothing else
constexpr int kRawDigit0 = 0x10020;

class Platform {
public:
    virtual ~Platform() = default;
    virtual const char* name() const = 0;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void poll(std::vector<KeyEvent>& out, bool& quit) = 0;
    // rotate180: true if the image must be turned upside down on its way out.
    virtual void present(const Canvas& c, bool rotate180) = 0;
    // Whether this backend normally needs a 180 degree turn (Miyoo panels are
    // mounted upside down; SDL on OnionOS already compensates).
    virtual bool defaultRotate() const = 0;
    virtual std::string rawKeyName(int raw) const = 0;
    virtual std::string describe() const = 0;  // multi-line diagnostics
    virtual uint32_t ticks() = 0;               // milliseconds
    virtual void waitFrame(uint32_t frameStartMs, bool idle) = 0;
    virtual bool headless() const { return false; }
    // The sharp PC picture (0.27.0): how many real pixels per game unit the window
    // wants (1 = the 640x480 picture, scaled up by the platform as before).
    virtual float desiredScale() const { return 1.f; }
    // Widescreen (0.46.0): how many game units wide the picture should be for the
    // window's shape (always 480 tall; 640 = 4:3 as before).
    virtual int desiredWidth() const { return 640; }
    // Settings the PC backends follow (0.27.0): "screen" (1 sharp, 0 original
    // 640x480), "controls" (0 keyboard & mouse, 1 controller), "fullscreen".
    // Called at start and whenever the Options screen changes one.
    virtual void configure(Settings& s) { (void)s; }
    // Holding SELECT for 3 s swaps fbdev <-> sdl (a rescue for a blank screen
    // on the Miyoo). Backends with nothing to swap to turn it off.
    virtual bool screenRescue() const { return true; }
};

std::unique_ptr<Platform> makeFbdevPlatform();
std::unique_ptr<Platform> makeSdlPlatform();
std::unique_ptr<Platform> makeHeadlessPlatform(const std::string& scriptPath, const std::string& outDir);
std::unique_ptr<Platform> makeWin32Platform();  // Windows builds only (nullptr elsewhere)
std::unique_ptr<Platform> makeSdl2Platform();   // Linux desktop / Steam Deck build only (TI4_SDL2)

// Shared helpers
struct timespec;
void monotonicNow(timespec* ts);
uint32_t monotonicMs();
void sleepMs(uint32_t ms);
// Moves `from` over `to`, replacing it (std::rename can't replace on Windows).
bool replaceFile(const std::string& from, const std::string& to);
