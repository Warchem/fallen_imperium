// League mode (0.80.0): a league being played - its standings, the table of each game, the faction draft, the game's
// Setup, and what a finished game means for the league (places, points, bonuses, the rivals' career memory, the league
// won or lost). Everything lives in the player profile (profile.cfg, keys "L.<league>.*" for a run, "R.<rival>.*" for a
// rival's memory of you); each league's game in progress has its own save file (league-<id>.ti4). No drawing here: the
// league scenes (app/scene_league.cpp) show it with app/league_ui.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "game/leagues.h"

class Settings;
class GameSession;

namespace leaguerun {

using league::LeagueDef;

// One of a league's standings: you, a named rival - or, in a league without rivals (The Grid), one of its programs.
struct Member {
    std::string id;  // "you", a rival id or "prog1".."prog7"
    std::string name;
    const league::RivalDef* rival = nullptr;
    int faction = -1;  // the rival's signature faction (-1 none / not in this league's pool)
    int arch = -1;     // ti::Arch; -1 random, -2 none (a program)
};
std::vector<Member> members(const LeagueDef* d);  // you first, then the rivals in the page's order
const Member* member(const std::vector<Member>& v, const std::string& id);

// The run
bool active(const std::string& id);  // started and not finished or given up
void begin(const std::string& id, uint32_t seed);  // a fresh run: game 1, every member on 0 points
void giveUp(const std::string& id);  // the run and its saved game are dropped (the rivals' memory stays)
int gamesPlayed(const std::string& id);
int points(const std::string& id, const std::string& who);
int lastPlace(const std::string& id, const std::string& who);  // -1 before the first game
int moved(const std::string& id, const std::string& who);      // places gained (+) / lost (-) in the standings last game
int played(const std::string& id, const std::string& who);     // games this run
std::string savePath(const std::string& id);
bool gameSaved(const std::string& id);
int keptFaction(const std::string& id);  // leagues without a draft: the faction you keep (-1 not chosen yet)
void setKeptFaction(const std::string& id, int f);
int galaxyChoice(const std::string& id);  // leagues with "Your choice": 1 draft, 0 random
void setGalaxyChoice(const std::string& id, int v);
// the member nearest you in the standings (ties: the one above you), "" with nobody else
std::string yourRival(const LeagueDef* d);
// the standings (you first), ranked by points (ties keep the members' order)
std::vector<std::string> ranking(const LeagueDef* d);

// The rivals' career memory of you (forever: also across runs and leagues)
struct Career {
    int games = 0, youAbove = 0, theyAbove = 0, broken = 0;
};
Career career(const std::string& rival);

// The next game
struct SeatPlan {
    std::string who;  // member id, "" = one of the table's other bots (not in the standings)
    std::string name;
    int faction = -1, arch = -1, level = 2;
    int signature = -1;  // a rival's own faction (it takes it in the draft when it can)
};
struct Table {
    std::vector<SeatPlan> seats;  // by seat (clockwise)
    int human = 0;
    std::string yourRival;
};
std::vector<int> factionPool(const LeagueDef* d);  // the factions this league deals (its modules, minus its bans)
Table planTable(const LeagueDef* d, uint32_t seed);  // who sits where, their personalities and difficulty
// the draft: the factions on offer (players + 3, favoured ones more likely; a league where you pick: the whole pool)
std::vector<int> offer(const LeagueDef* d, const Table& t, uint32_t seed, bool wholePool);
// a bot's pick from what is left: a rival takes its signature faction when it is there, else favoured ones more likely
int botPick(const LeagueDef* d, const SeatPlan& s, const std::vector<int>& left, uint32_t seed);
// leagues without a draft: you keep your faction, rivals theirs, the other bots get one from the pool
void dealFactions(const LeagueDef* d, Table& t, int yours, uint32_t seed);
// the draft of the next game keeps its seed until that game starts (backing out and in doesn't deal a new offer)
uint32_t draftSeed(const std::string& id, uint32_t fresh);
// the game's Setup (speaker -1 = at random); also stores the seat plan in the profile for the results
ti::Setup makeSetup(const LeagueDef* d, const Table& t, int speaker, const Settings& st, uint32_t seed);

// A finished game
struct Row {
    std::string who, name;  // who "" = one of the other bots
    int faction = -1;
    bool you = false, named = false;
    int vp = 0, place = 0, placePts = 0, bonusPts = 0, total = 0, move = 0;
};
struct Outcome {
    std::string league;
    int game = 0;  // the game's index (0 = the first)
    std::vector<Row> rows;  // by place
    std::vector<std::pair<std::string, bool>> yourBonuses;  // the bonuses this league can give, and whether you got them
    std::string speaker, quote, memory;  // a rival's word after the game
    int speakerFaction = -1;
    int ended = 0;  // 0 the league goes on, 1 you won it, 2 lost (the game limit came, or a rival reached the target first)
    std::string winner;  // the league's winner when it ended
    int points = 0, games = 0;  // your points / games played when it ended
    std::vector<std::pair<std::string, int>> finalTable;  // (member id, points) when it ended
};
// the names of the commanders at the table of the game in progress, by seat ("" for the other bots and for you)
std::vector<std::string> seatNames(const std::string& id);
// GameScene at the end of a league game: places, points and bonuses go into the league (once - the save is deleted)
bool recordGame(GameSession& s);
// the last recorded game's outcome, for the results screen (false: none waiting)
bool takeOutcome(Outcome& o);
// a league won: its finish is recorded (emblem, colour, the leagues it opens) and the run cleared; lost: back to game 1
void settle(const Outcome& o, uint32_t seed);

}  // namespace leaguerun
