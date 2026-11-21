// The player profile (0.80.0, roadmap Stage 17 "needs a player profile file (progress / unlocks)"): what the player has
// earned - the leagues finished (and so their emblems and colours), the league in progress with its standings, the rivals'
// career memory of the player - in profile.cfg next to the game (key=value lines, core/settings). Unlocks follow from it;
// the "unlock everything" switch (Options, for testing) opens everything without earning it.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace profile {

void load(const std::string& path);  // main() calls it once (headless tests: the output folder's profile.cfg)
bool save();

bool unlockAll();  // the testing switch
void setUnlockAll(bool on);

bool finished(const std::string& league);
std::vector<std::string> finishedLeagues();  // in the order they were finished
void markFinished(const std::string& league);
bool leagueOpen(const std::string& league);  // open from the start, or the league before it finished (or the switch)
// the emblems earned (key, the league's colour), in the order they were earned
std::vector<std::pair<std::string, uint32_t>> earnedEmblems();
// player colour c may be picked: the classic eight always; the others once the league that gives them is finished
bool colourUnlocked(int c);
std::string colourSource(int c);  // "Finish the Ion Storm Circuit", or "" when no league gives it

// a raw value of the profile (league runs and rival memory keep their numbers here; see scene_league.cpp)
std::string get(const std::string& key, const std::string& def = "");
int getInt(const std::string& key, int def = 0);
void set(const std::string& key, const std::string& value);
void setInt(const std::string& key, int v);
void erase(const std::string& prefix);  // every key starting with prefix

}  // namespace profile
