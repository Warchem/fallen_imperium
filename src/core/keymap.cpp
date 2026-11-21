#include "core/keymap.h"

#include <algorithm>
#include <cstdio>

#include "core/settings.h"

namespace keymap {
namespace {

const char* const kActIds[ActCount] = {"UP",   "DOWN", "LEFT", "RIGHT",  "A",     "B",     "X",       "Y",        "L1",       "R1",
                                       "L2",   "R2",   "SELECT", "START", "MENU", "CAMUP", "CAMDOWN", "CAMLEFT", "CAMRIGHT", "CAMFAST"};
const char* const kActNames[ActCount] = {
    "Cursor up",        "Cursor down",          "Cursor left",         "Cursor right",       "Choose / confirm (A)",
    "Back / undo (B)",  "Objectives (X)",       "Log / suggestion (Y)", "Zoom out, fewer (L1)", "Zoom in, more (R1)",
    "Side panel (L2)",  "View the map (R2)",    "Players overview (SELECT)", "Confirm editors (START)", "Game menu (MENU)",
    "Camera up",        "Camera down",          "Camera left",         "Camera right",       "Camera faster (hold)"};

std::vector<std::vector<int>> defaults() {
    std::vector<std::vector<int>> d(ActCount);
    d[Up] = {KUp};
    d[Down] = {KDown};
    d[Left] = {KLeft};
    d[Right] = {KRight};
    d[A] = {KSpace};
    d[B] = {KBackspace};
    d[X] = {'X'};
    d[Y] = {'V'};
    d[L1] = {'Q'};
    d[R1] = {'E'};
    d[L2] = {'Z'};
    d[R2] = {KTab, 'C'};  // Tab: view the map
    d[Select] = {'R'};
    d[Start] = {KEnter, KNumEnter};
    d[Menu] = {KEsc};  // Esc: the game menu (owner, 0.27.0)
    d[CamUp] = {'W'};
    d[CamDown] = {'S'};
    d[CamLeft] = {'A'};
    d[CamRight] = {'D'};
    d[CamFast] = {KShift};  // (0.72.0, owner: WASD speed-up with Shift)
    return d;
}

std::vector<std::vector<int>>& table() {
    static std::vector<std::vector<int>> t = defaults();
    return t;
}
uint32_t g_gen = 1;
int g_lastKey = KNone;

}  // namespace

int actOf(int key) {
    if (key == KNone) return -1;
    const auto& t = table();
    for (int a = 0; a < ActCount; ++a)
        for (int k : t[size_t(a)])
            if (k == key) return a;
    return -1;
}

int route(int key, bool controller) {
    const int a = actOf(key);
    if (a < 0) return -1;
    if (a < CamUp) return a;  // a button
    if (a == CamFast) return controller ? -1 : -20;
    if (controller) return Up + (a - CamUp);  // with a controller the camera keys are the D-pad
    return -10 - (a - CamUp);
}

const std::vector<int>& keys(int act) {
    static const std::vector<int> none;
    return act >= 0 && act < ActCount ? table()[size_t(act)] : none;
}

bool bindable(int key) {
    if (key == KNone) return false;
    if (key >= '0' && key <= '9') return false;  // they type into number fields (the galaxy seed)
    if (key >= KNum0 && key <= KNum0 + 9) return false;
    if (key == KF1 + 10) return false;  // F11: full screen
    return true;
}

int bind(int act, int key) {
    if (act < 0 || act >= ActCount || !bindable(key)) return -1;
    auto& t = table();
    const std::vector<int> old = t[size_t(act)];
    int loser = -1;
    for (int a = 0; a < ActCount; ++a) {
        if (a == act) continue;
        auto& v = t[size_t(a)];
        const auto it = std::find(v.begin(), v.end(), key);
        if (it == v.end()) continue;
        v.erase(it);
        loser = a;
        if (v.empty()) {  // the two swap
            for (int k : old)
                if (k != key && actOf(k) == act) {
                    v.push_back(k);
                    break;
                }
        }
    }
    t[size_t(act)] = {key};
    ++g_gen;
    return loser;
}

void reset() {
    table() = defaults();
    ++g_gen;
}

bool isDefault() { return table() == defaults(); }

uint32_t generation() { return g_gen; }

void load(const Settings& s) {
    auto& t = table();
    t = defaults();
    const std::string spec = s.get("keymap", "");
    // "R2=TAB,C;A=F": the actions changed from the defaults
    size_t i = 0;
    while (i < spec.size()) {
        size_t semi = spec.find(';', i);
        if (semi == std::string::npos) semi = spec.size();
        const std::string item = spec.substr(i, semi - i);
        i = semi + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string id = item.substr(0, eq);
        int act = -1;
        for (int a = 0; a < ActCount; ++a)
            if (id == kActIds[a]) act = a;
        if (act < 0) continue;
        std::vector<int> ks;
        size_t j = eq + 1;
        while (j <= item.size()) {
            size_t comma = item.find(',', j);
            if (comma == std::string::npos) comma = item.size();
            const int k = keyByName(item.substr(j, comma - j));
            if (k != KNone && bindable(k)) ks.push_back(k);
            j = comma + 1;
        }
        if (ks.empty()) continue;
        for (int k : ks)  // (a key belongs to one action: the saved one wins)
            for (auto& v : t) v.erase(std::remove(v.begin(), v.end(), k), v.end());
        t[size_t(act)] = ks;
    }
    for (int a = 0; a < ActCount; ++a)  // (a hand-edited file left an action with nothing: its defaults, where free)
        if (t[size_t(a)].empty())
            for (int k : defaults()[size_t(a)])
                if (actOf(k) < 0) t[size_t(a)].push_back(k);
    ++g_gen;
}

void save(Settings& s) {
    const auto d = defaults();
    const auto& t = table();
    std::string spec;
    for (int a = 0; a < ActCount; ++a) {
        if (t[size_t(a)] == d[size_t(a)]) continue;
        if (!spec.empty()) spec += ';';
        spec += kActIds[a];
        spec += '=';
        for (size_t k = 0; k < t[size_t(a)].size(); ++k) {
            if (k) spec += ',';
            spec += shortName(t[size_t(a)][k]);
        }
    }
    s.set("keymap", spec);
}

const char* actName(int act) { return act >= 0 && act < ActCount ? kActNames[act] : "?"; }

std::string shortName(int key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, char(key));
    if (key >= KF1 && key < KF1 + 12) return "F" + std::to_string(key - KF1 + 1);
    if (key >= KNum0 && key < KNum0 + 10) return "NUM" + std::to_string(key - KNum0);
    switch (key) {
        case KBackspace: return "BKSP";
        case KTab: return "TAB";
        case KEnter: return "ENTER";
        case KEsc: return "ESC";
        case KSpace: return "SPACE";
        case KUp: return "UP";
        case KDown: return "DOWN";
        case KLeft: return "LEFT";
        case KRight: return "RIGHT";
        case KShift: return "SHIFT";
        case KCtrl: return "CTRL";
        case KAlt: return "ALT";
        case KInsert: return "INS";
        case KDelete: return "DEL";
        case KHome: return "HOME";
        case KEnd: return "END";
        case KPageUp: return "PGUP";
        case KPageDown: return "PGDN";
        case KCapsLock: return "CAPS";
        case KNumAdd: return "NUM+";
        case KNumSub: return "NUM-";
        case KNumMul: return "NUM*";
        case KNumDiv: return "NUM/";
        case KNumDot: return "NUM.";
        case KNumEnter: return "NUMENTER";
        default: break;
    }
    if (key > 32 && key < 127) return std::string(1, char(key));  // punctuation
    return "?";
}

std::string longName(int key) {
    switch (key) {
        case KBackspace: return "Backspace";
        case KTab: return "Tab";
        case KEnter: return "Enter";
        case KEsc: return "Esc";
        case KSpace: return "Space";
        case KUp: return "Up arrow";
        case KDown: return "Down arrow";
        case KLeft: return "Left arrow";
        case KRight: return "Right arrow";
        case KShift: return "Shift";
        case KCtrl: return "Ctrl";
        case KAlt: return "Alt";
        case KInsert: return "Insert";
        case KDelete: return "Delete";
        case KHome: return "Home";
        case KEnd: return "End";
        case KPageUp: return "Page Up";
        case KPageDown: return "Page Down";
        case KCapsLock: return "Caps Lock";
        case KNumEnter: return "Enter (number pad)";
        default: break;
    }
    if (key >= KNum0 && key < KNum0 + 10) return "Number pad " + std::to_string(key - KNum0);
    if (key >= KNumAdd && key <= KNumDot) return "Number pad " + shortName(key).substr(3);
    return shortName(key);
}

std::string label(int act) {  // (a hint badge: the first key)
    const std::vector<int>& k = keys(act);
    return k.empty() ? std::string("-") : shortName(k[0]);
}

std::string allKeys(int act) {
    std::string s;
    for (int k : keys(act)) {
        if (!s.empty()) s += " / ";
        s += longName(k);
    }
    return s.empty() ? std::string("(no key)") : s;
}

int keyByName(const std::string& n) {
    if (n.empty()) return KNone;
    for (int k = 1; k < 512; ++k)
        if (shortName(k) != "?" && shortName(k) == n) return k;
    return KNone;
}

void noteKey(int key) { g_lastKey = key; }
int takeKey() {
    const int k = g_lastKey;
    g_lastKey = KNone;
    return k;
}

}  // namespace keymap
