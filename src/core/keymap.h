// Key bindings (0.72.0, owner 2026-11-19: "the key bindings" in the main menu's Options). Windows and the Steam Deck build
// turn their own key codes into these portable keys, then ask route() what the key does: a button, the map camera, or
// nothing. The player can give any action another key (Options > Controls and key bindings); saved in settings.cfg as
// "keymap" (only the actions changed from the defaults). The Miyoo doesn't use it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Settings;

namespace keymap {

// Portable keys: letters 'A'..'Z' and digits '0'..'9' as themselves, punctuation as its character, the rest from 256 up.
enum Key : int {
    KNone = 0,
    KBackspace = 8, KTab = 9, KEnter = 13, KEsc = 27, KSpace = 32,
    KUp = 256, KDown, KLeft, KRight, KShift, KCtrl, KAlt, KInsert, KDelete, KHome, KEnd, KPageUp, KPageDown, KCapsLock,
    KF1 = 300,  // .. KF1 + 11 = F12
    KNum0 = 400,  // .. KNum0 + 9
    KNumAdd = 410, KNumSub, KNumMul, KNumDiv, KNumDot, KNumEnter,
};

// What can be bound: the game's buttons (the same numbers as Btn: Up .. Menu), then the keyboard camera.
enum Act : int {
    Up, Down, Left, Right, A, B, X, Y, L1, R1, L2, R2, Select, Start, Menu,  // = Btn
    CamUp, CamDown, CamLeft, CamRight, CamFast,
    ActCount
};

// What a key press means: a Btn (>= 0), the camera (-10 up, -11 down, -12 left, -13 right), the camera's speed-up (-20),
// or nothing (-1). With a controller the camera keys are the D-pad and the speed-up does nothing.
int route(int key, bool controller);

const std::vector<int>& keys(int act);
int actOf(int key);  // -1: no action
// The action gets only `key`; an action that had it loses it, and if it is left with no key it takes `act`'s old one
// (the two swap). Returns the action that lost the key (-1: none).
int bind(int act, int key);
bool bindable(int key);  // (not the digits - they type into number fields - nor F11, full screen)
void reset();            // the defaults
bool isDefault();
void load(const Settings& s);
void save(Settings& s);
uint32_t generation();  // goes up with every change (for labels worked out from the bindings)

const char* actName(int act);  // "Choose / confirm"
std::string shortName(int key);  // on a hint badge: "SPACE", "Q", "UP"
std::string longName(int key);   // in the list: "Space", "Q", "Up arrow"
std::string label(int act);      // on a hint badge: the action's first key ("TAB")
std::string allKeys(int act);    // in the list: all its keys ("Tab / C")
int keyByName(const std::string& shortName);

// The last key that went down (for "press a key"): platforms report every key here; take() returns it once (KNone if
// none since the last call).
void noteKey(int key);
int takeKey();

}  // namespace keymap
