// The opponent on your offer screen (0.64.0, owner's document: "Your negotiations: an animated opponent who talks
// about you"; roadmap: "lines by neighbour, promise, deal, clash, attitude"). When you open an offer to a computer player,
// its portrait comes on and it says a line about YOU - picked from what lies between you, things you can see for yourself:
// a promise you broke, a fight you started, your lead, promises kept, deals made, a shared border; after the round-3 reveal
// also how it feels about you. Display only (nothing here touches the game), so saves replay exactly as before.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/state.h"

namespace negotalk {

enum class Why : int { Hello, Neighbour, Partner, Kept, Broken, Clash, Leader, Friendly, Wary, Hostile, Count };

struct Mood {
    Why why = Why::Hello;
    std::vector<std::string> tags;  // what lies between you, short ("Neighbours", "You broke a promise", ...)
};
// How computer player `bot` sees player `you` (only facts both of you know; the attitude after the reveal).
Mood moodOf(const ti::Game& g, int bot, int you);
// What `bot` says to `you` as the offer screen opens ({them} = your faction's name), or "" when it has nothing.
std::string quip(const ti::Game& g, int bot, int you, Why why, uint32_t seed);
const char* whyName(Why w);

}  // namespace negotalk
