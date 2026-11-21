// League mode screens (0.80.0): the Tournament ladder, a league in progress, the faction draft, a game's results, a finished
// league, the rivals' dossiers, and the earned emblems under the title menu. Drawing only: each screen draws from a small
// view (filled by the league scenes from the profile - or by the screen tests with sample data). Laid out for 640 x 480.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "game/leagues.h"
#include "gfx/canvas.h"

namespace leagueui {

using league::LeagueDef;
using league::RivalDef;

// The mouse: pointing at a row, button or card of the screen being drawn sets *target to its index (the scene's
// selection; a click also presses A). nullptr: nothing to point at.
void setPickTarget(int* target);

enum RowState { kLocked, kOpen, kInProgress, kFinished };
struct LadderEntry {
    const LeagueDef* def = nullptr;
    RowState state = kLocked;
    int game = 0, points = 0;  // in progress: games played, your points
};
void drawBackdrop(Canvas& c, uint32_t now);
// A: the leagues as a list, the chosen one in detail beside it
void drawLadder(Canvas& c, const std::vector<LadderEntry>& rows, int sel, uint32_t now);
// B: the leagues as a map - the main ladder across the top, the side leagues hanging under the league that opens them
void drawLadderMap(Canvas& c, const std::vector<LadderEntry>& rows, int sel, uint32_t now);
// a short note over the ladder ("Locked: finish the Bronze League first")
void drawToast(Canvas& c, const std::string& text);

struct Standing {
    std::string name;
    int faction = -1;  // -1: not chosen yet
    int arch = -1;     // ti::Arch; -1 you
    bool you = false;
    int points = 0, lastPlace = -1, move = 0;  // lastPlace 0 = first; move: places gained (+) or lost (-) last game
};
struct HubView {
    const LeagueDef* def = nullptr;
    int game = 0;  // games played
    std::vector<Standing> table;
    std::string yourRival;  // the rival named for the next game
    int rivalGap = 0;       // its points minus yours
    std::string talker, quote;  // a rival's word before the game
    int talkerFaction = -1;
    int sel = 0;  // 0 play, 1 rivals, 2 rules, 3 give up
    bool saved = false;   // a game of this league is saved: "Continue game N"
    int galaxy = -1;      // leagues with "Your choice": 1 draft, 0 random (-1: the league decides)
    bool pickFirst = false;  // a league where you keep one faction, not chosen yet
};
void drawHub(Canvas& c, const HubView& v, uint32_t now);

struct DraftSeat {
    std::string name;
    bool you = false;
    int faction = -1;  // picked (-1 not yet)
};
struct DraftView {
    const LeagueDef* def = nullptr;
    int game = 0;
    std::vector<DraftSeat> order;  // picking order (the first is random, then clockwise)
    int current = 0;               // whose pick it is
    std::vector<int> offer;        // factions on offer
    int sel = 0;                   // the highlighted one in `offer`
    bool done = false;             // everyone has picked
    bool pickOnly = false;         // a league where you keep one faction: only you pick, once
};
void drawDraft(Canvas& c, const DraftView& v, uint32_t now);

struct ResultRow {
    std::string name;
    int faction = -1;
    bool you = false, named = true;  // named: in the standings (a rival or you); others are the table's filler seats
    int vp = 0, place = 0, placePts = 0, bonusPts = 0, total = 0, move = 0;
};
struct ResultView {
    const LeagueDef* def = nullptr;
    int game = 0;
    std::vector<ResultRow> rows;
    std::vector<std::pair<std::string, bool>> yourBonuses;  // every bonus and whether you got it
    std::string speaker, quote, memory;
    int speakerFaction = -1;
};
void drawResults(Canvas& c, const ResultView& v, uint32_t now);

struct WonView {
    const LeagueDef* def = nullptr;
    int points = 0, games = 0;
    std::vector<const LeagueDef*> opens;
    bool won = true;  // false: the game limit came first - the league is lost (try again)
    std::string winner;
    std::vector<Standing> table;  // (lost: the final standings)
};
void drawWon(Canvas& c, const WonView& v, uint32_t now);

struct RivalCard {
    const RivalDef* def = nullptr;
    int games = 0, youAbove = 0, theyAbove = 0, broken = 0;  // career record with you
};
void drawRivals(Canvas& c, const LeagueDef* league, const std::vector<RivalCard>& cards, int sel, uint32_t now);
// the league's rules: points, bonuses, its table and content, the reward
void drawRules(Canvas& c, const LeagueDef* league, uint32_t now);
// a yes / no question over the screen (A yes, B no)
void drawConfirm(Canvas& c, const std::string& title, const std::string& text, const std::string& yes);

// the title screen's row of earned medals (key, colour), centred at cy
void drawEmblemRow(Canvas& c, int cy, const std::vector<std::pair<std::string, uint32_t>>& earned, uint32_t now);
// every emblem (the art sheet)
void drawEmblemSheet(Canvas& c, uint32_t now);

}  // namespace leagueui
