// League mode (0.80.0, roadmap Stage 17 + the owner's League Designer page): the Tournament's leagues, their named rival
// commanders, the points for each place and the bonus points - generated from the page by tools/gen_leagues.py
// (src/game/gen/leagues.inc).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace league {

enum Difficulty { kEasy, kNormal, kHard, kCompetitive, kAI, kRising, kMixed };
enum Galaxy { kGalaxyRandom, kGalaxyDraft, kGalaxySeeded, kGalaxyChoice };
enum Pool { kPoolAny, kPoolFavoured, kPoolWeighted, kPoolPlayer };

struct LeagueDef {
    const char* id;          // stable key (the profile file uses it)
    const char* name;
    uint32_t color;          // the league's own colour (cards, the ladder)
    int order;
    const char* after;       // the league whose finish opens it ("" = open from the start)
    const char* unlockText;  // as the owner wrote it
    int target, maxGames, players, vp;
    int difficulty;  // Difficulty
    int galaxy;      // Galaxy
    int modules;     // engine/state.h kModCodex ... bits
    bool factionDraft, kingslay, freeRefills, x1;
    int pool;                       // Pool
    const char* favoured;           // faction keys, comma separated
    const char* banned;
    const char* persFav, *persBan;  // archetype keys
    int rivals;                     // named rivals at each table
    const char* reward, *notes;
    const char* emblem;  // emblems:: key ("" none)
    int colour;          // the player colour it unlocks (pcolours index), -1 none
    bool builder;        // the Custom League (the Tournament Builder)
};
struct RivalDef {
    const char* id, *name, *league;
    int arch;  // ti::Arch, -1 none
    const char* faction;
    const char* grudge, *taunt;
};
struct BonusDef {
    const char* id, *label;
    int points;
};

const std::vector<const LeagueDef*>& all();  // in the page's order
const LeagueDef* find(const std::string& id);
std::vector<const RivalDef*> rivalsOf(const std::string& league);
const RivalDef* rival(const std::string& id);
int placePoints(int place);  // 0 = first
const std::vector<BonusDef>& bonuses();
const char* pitch();
const char* tieRule();  // "Shared places split the points, rounded up. ..."
const char* difficultyName(int d);
const char* galaxyName(int g);
bool listHas(const char* list, const std::string& key);  // "a,b,c" contains key

}  // namespace league
