#include "core/input.h"

#include <cstring>
#include <vector>

#include "core/keymap.h"

namespace {
constexpr uint32_t kRepeatDelay = 280;
constexpr uint32_t kRepeatRate = 70;

bool repeats(Btn b) {
    switch (b) {
        case Btn::Up: case Btn::Down: case Btn::Left: case Btn::Right:
        case Btn::L1: case Btn::R1:
            return true;
        default:
            return false;
    }
}
}  // namespace

const char* btnName(Btn b) {
    static const char* names[kBtnCount] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L1",
                                           "R1", "L2", "R2", "SELECT", "START", "MENU", "POWER",
                                           "VOL+", "VOL-"};
    int i = int(b);
    return (i >= 0 && i < kBtnCount) ? names[i] : "?";
}

// ---------------------------------------------------------------- key names
namespace {
bool g_keyboard = false;

struct KeyName {
    const char* pad;
    const char* key;
};
// Longer names first so "L1/R1" is matched before "L1".
// (0.27.0, owner: Esc = the game menu, Backspace = back, Tab = view the map;
// WASD moves the map camera, the arrow keys move the cursor and through lists)
// (0.72.0: worked out from the key bindings, Options > Controls and key bindings; the defaults give
// L1/R1 Q/E, L2/R2 Z/TAB, D-PAD ARROWS/WASD, SELECT R, START ENTER, MENU ESC, A SPACE, B BKSP, X X, Y V)
struct DynName {
    std::string pad, key;
};
// four directions' keys as one name: ARROWS, WASD, or the letters (up left down right: "IJKL")
std::string dirName(int up, int down, int left, int right) {
    using namespace keymap;
    auto first = [](int act) { return keys(act).empty() ? int(KNone) : keys(act)[0]; };
    const int u = first(up), d = first(down), l = first(left), r = first(right);
    if (u == KUp && d == KDown && l == KLeft && r == KRight) return "ARROWS";
    const std::string a = shortName(u), b = shortName(l), c = shortName(d), e = shortName(r);
    if (a.size() == 1 && b.size() == 1 && c.size() == 1 && e.size() == 1) return a == "W" && b == "A" && c == "S" && e == "D" ? "WASD" : a + b + c + e;
    return a + "," + b + "," + c + "," + e;
}
const std::vector<DynName>& keyNames() {
    static std::vector<DynName> names;
    static uint32_t gen = 0;
    if (gen == keymap::generation()) return names;
    gen = keymap::generation();
    using namespace keymap;
    names = {{"L1/R1", label(L1) + "/" + label(R1)},
             {"L2/R2", label(L2) + "/" + label(R2)},
             {"D-PAD", dirName(Up, Down, Left, Right) + "/" + dirName(CamUp, CamDown, CamLeft, CamRight)},
             {"SELECT", label(Select)},
             {"START", label(Start)},
             {"MENU", label(Menu)},
             {"L1", label(L1)},
             {"R1", label(R1)},
             {"L2", label(L2)},
             {"R2", label(R2)},
             {"A", label(A)},
             {"B", label(B)},
             {"X", label(X)},
             {"Y", label(Y)}};
    return names;
}

bool g_deck = false;
const KeyName kDeckNames[] = {{"SELECT", "L3"}, {"START", "VIEW"}};

bool wordChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }

bool endsWith(const std::string& s, const char* tail) {
    std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
}  // namespace

void setKeyboardKeys(bool on) { g_keyboard = on; }
void setDeckKeys(bool on) { g_deck = on; }
bool keyboardKeys() { return g_keyboard; }

std::string keyLabel(const std::string& button) {
    if (g_deck && !g_keyboard) {
        for (const KeyName& k : kDeckNames)
            if (button == k.pad) return k.key;
        return button;
    }
    if (!g_keyboard) return button;
    for (const DynName& k : keyNames())
        if (button == k.pad) return k.key;
    return button;
}

std::string keyText(const std::string& text) {
    if (g_deck && !g_keyboard) {  // only the two names that differ on the Deck
        std::string out = text;
        for (const KeyName& k : kDeckNames) {
            const std::string pad(k.pad);
            size_t at = 0;
            while ((at = out.find(pad, at)) != std::string::npos) {
                const bool before = at == 0 || !wordChar(out[at - 1]);
                const bool after = at + pad.size() >= out.size() || !wordChar(out[at + pad.size()]);
                if (before && after) {
                    out.replace(at, pad.size(), k.key);
                    at += std::strlen(k.key);
                } else {
                    at += pad.size();
                }
            }
        }
        return out;
    }
    if (!g_keyboard) return text;
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        bool done = false;
        if (i == 0 || !wordChar(text[i - 1])) {
            for (const DynName& k : keyNames()) {
                const std::string& pad = k.pad;
                if (text.compare(i, pad.size(), pad) != 0) continue;
                size_t end = i + pad.size();
                if (end < text.size() && wordChar(text[end])) continue;
                // One-letter buttons only where a sentence clearly means a button.
                if (pad.size() == 1) {
                    bool before = endsWith(out, "press ") || endsWith(out, "Press ") || endsWith(out, "for ") ||
                                  endsWith(out, "Hold ") || endsWith(out, "hold ");
                    bool after = text.compare(end, 3, " = ") == 0 || text.compare(end, 4, " to ") == 0 ||
                                 // 0.43.0 (owner: the Pass box said "A: Pass"): "A: ..." hints at a line start or after a gap
                                 (text.compare(end, 2, ": ") == 0 && (i == 0 || endsWith(out, "  ")));
                    if (!before && !after) continue;
                }
                out += k.key;
                i = end;
                done = true;
                break;
            }
        }
        if (!done) out += text[i++];
    }
    return out;
}

void Input::beginFrame() {
    for (int i = 0; i < kBtnCount; ++i) pressed_[i] = released_[i] = false;
    any_ = false;
    mouse_.moved = mouse_.leftPress = mouse_.click = mouse_.rightClick = false;
    mouse_.dragX = mouse_.dragY = mouse_.wheel = 0.f;
    typed_.clear();
}

void Input::handle(Btn b, bool down, uint32_t now) {
    if (!valid(b)) return;
    int i = int(b);
    any_ = true;
    if (down && !held_[i]) {
        held_[i] = true;
        pressed_[i] = true;
        downAt_[i] = now;
        nextRepeat_[i] = now + kRepeatDelay;
    } else if (!down && held_[i]) {
        held_[i] = false;
        released_[i] = true;
    }
}

void Input::update(uint32_t now) {
    for (int i = 0; i < kBtnCount; ++i) {
        if (held_[i] && repeats(Btn(i)) && int32_t(now - nextRepeat_[i]) >= 0) {
            pressed_[i] = true;
            any_ = true;
            nextRepeat_[i] = now + kRepeatRate;
        }
    }
}
