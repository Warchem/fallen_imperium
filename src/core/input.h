// Logical buttons of the Miyoo Mini Plus and per-frame button state.
#pragma once
#include <cstdint>
#include <string>

enum class Btn : int {
    Up, Down, Left, Right,
    A, B, X, Y,
    L1, R1, L2, R2,
    Select, Start, Menu,
    Power, VolUp, VolDown,
    Count,
    None = -1
};
constexpr int kBtnCount = int(Btn::Count);
const char* btnName(Btn b);

// On-screen button names. The Miyoo shows its own buttons (A, START, L1...);
// the Windows version turns them into the keyboard keys they are mapped to.
void setKeyboardKeys(bool on);
void setDeckKeys(bool on);  // Steam Deck build: START -> VIEW, SELECT -> L3
bool keyboardKeys();
std::string keyLabel(const std::string& button);  // one hint badge: "A" -> "SPACE"
std::string keyText(const std::string& text);     // button names inside a sentence: "press A" -> "press SPACE"

// The mouse (Windows / Steam Deck, 0.27.0). Positions are in the game's
// 640x480 units whatever the window's size.
struct Mouse {
    bool active = false;         // a mouse has been used
    bool inside = false;         // (0.72.0) the pointer is over the window (and it has the focus): edge scrolling
    float x = -1.f, y = -1.f;
    bool moved = false;          // this frame
    bool left = false, right = false;  // held
    bool leftPress = false;      // went down this frame
    bool click = false;          // left button let go this frame (not after a drag)
    bool rightClick = false;     // right button let go without dragging: back
    bool dragging = false;       // right button held and moved: the camera follows
    float dragX = 0, dragY = 0;  // right-drag movement this frame
    float wheel = 0;             // wheel notches this frame (+ = away from you; touchpads in fractions)
};

class Input {
public:
    void beginFrame();
    void handle(Btn b, bool down, uint32_t nowMs);
    void update(uint32_t nowMs);  // generates auto-repeat for the D-pad and shoulders

    bool held(Btn b) const { return valid(b) && held_[int(b)]; }
    bool pressed(Btn b) const { return valid(b) && pressed_[int(b)]; }  // includes auto-repeat
    bool released(Btn b) const { return valid(b) && released_[int(b)]; }
    uint32_t heldFor(Btn b, uint32_t nowMs) const { return held(b) ? nowMs - downAt_[int(b)] : 0; }
    bool anyEvent() const { return any_; }
    // Map zoom this frame: R1 = in (+1), L1 = out (-1). (The wheel zooms smoothly: mouse().wheel.)
    int zoom() const {
        const int z = (pressed(Btn::R1) ? 1 : 0) - (pressed(Btn::L1) ? 1 : 0);
        return z > 0 ? 1 : z < 0 ? -1 : 0;
    }
    // Keyboard & mouse (0.27.0): WASD moves the map camera (-1..1 each way) on screens with a map.
    float panX() const { return panX_; }
    float panY() const { return panY_; }
    void setPan(float x, float y) {
        panX_ = x;
        panY_ = y;
        if (x != 0.f || y != 0.f) any_ = true;
    }
    const Mouse& mouse() const { return mouse_; }
    // (0.63.0) digits typed on the keyboard this frame, in order (number fields)
    const std::string& typed() const { return typed_; }
    void type(char ch) {
        typed_ += ch;
        any_ = true;
    }
    Mouse& mouseState() { return mouse_; }
    void touch() { any_ = true; }

private:
    static bool valid(Btn b) { return int(b) >= 0 && int(b) < kBtnCount; }
    bool held_[kBtnCount] = {};
    bool pressed_[kBtnCount] = {};
    bool released_[kBtnCount] = {};
    uint32_t downAt_[kBtnCount] = {};
    uint32_t nextRepeat_[kBtnCount] = {};
    bool any_ = false;
    float panX_ = 0, panY_ = 0;
    Mouse mouse_;
    std::string typed_;
};
