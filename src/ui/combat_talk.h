// Combat reactions (0.71.0, owner 2026-11-19: "in the fight, whether space or ground combat, the graphic of the
// participating factions appear outside the edges of the battle view. The attacker will say something first, then the
// defender"; "no waiting"; one fight in three). Both portraits come in at the lower corners of the battle area, outside
// the hex (attacker left, defender right); a speech box under the hex types the attacker's line, then the defender's
// answer, with the speaker lit and the listener dimmed. Display only: the lines are chosen by hashes, never the game's dice.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "engine/state.h"

#include "gfx/canvas.h"
#include "ui/speech.h"

namespace combattalk {

struct Side {
    int faction = -1;
    uint32_t col = 0;
    std::string name;  // shown over the words ("BARONY OF LETNEV")
    std::string tag;   // over the portrait ("ATTACKER")
    std::string line;  // what it says ({them} already filled in)
};

class Talk {
public:
    void start(const Side& attacker, const Side& defender, uint32_t now, uint32_t seed);
    void clear() { on_ = false; }
    bool active(uint32_t now) const;
    // 0 when the attacker's words start, 1 when the defender's do (once each), else -1: for the voices
    int voiceDue(uint32_t now);
    // area: the battle area the space / ground battle is drawn in (the hex fills its height); bottom: the lowest y
    // the portraits and the box may use
    void draw(Canvas& c, Rect area, int bottom, uint32_t now);

private:
    void layoutLines(int textW, int textH);
    uint32_t endTime() const;
    Side s_[2];
    speech::Speech sp_[2];
    int laidW_ = -1, laidH_ = -1;
    uint32_t t0_ = 0, seed_ = 0, startB_ = 0;
    bool on_ = false, said_[2] = {false, false};
};

// What a game has had said so far (per faction: the lines used), so the same line comes back as late as possible.
struct Memory {
    std::map<int, std::set<std::string>> said;
};

// The line player `spk` says to player `lis` as a fight starts (attacker = spk attacks; space or ground), picked in this
// order (owner 15:27 / 15:35): its home attacked (the defender), a fight at Mecatol Rex, a rematch (the two fought in an
// earlier turn) - each the first time it happens, then one time in four - then the line written for that rival (2 in 3),
// then its usual lines; lines not said yet first. combatText: the fight's log line (to find earlier fights).
std::string chooseLine(const ti::Game& g, int spk, int lis, bool attacker, bool space, int sys, const std::string& combatText,
                       uint32_t seed, Memory& mem);
// Whether players a and b fought in an earlier turn (from the game log, so it survives saving and loading).
bool foughtBefore(const ti::Game& g, int a, int b, const std::string& combatText);

}  // namespace combattalk
