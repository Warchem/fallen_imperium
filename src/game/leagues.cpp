// League mode data - see leagues.h.
#include "game/leagues.h"

#include <cstring>

namespace league {
namespace {
#include "game/gen/leagues.inc"
}  // namespace

const std::vector<const LeagueDef*>& all() {
    static std::vector<const LeagueDef*> v;
    if (v.empty())
        for (const LeagueDef& l : kLeagues) v.push_back(&l);
    return v;
}
const LeagueDef* find(const std::string& id) {
    for (const LeagueDef& l : kLeagues)
        if (id == l.id) return &l;
    return nullptr;
}
std::vector<const RivalDef*> rivalsOf(const std::string& lg) {
    std::vector<const RivalDef*> v;
    for (const RivalDef& r : kRivals)
        if (lg == r.league) v.push_back(&r);
    return v;
}
const RivalDef* rival(const std::string& id) {
    for (const RivalDef& r : kRivals)
        if (id == r.id) return &r;
    return nullptr;
}
int placePoints(int place) { return place >= 0 && place < 8 ? kPlacePoints[place] : 0; }
const std::vector<BonusDef>& bonuses() {
    static std::vector<BonusDef> v(std::begin(kBonuses), std::end(kBonuses));
    return v;
}
const char* pitch() { return kLeaguePitch; }
const char* tieRule() { return kLeagueTieRule; }
const char* difficultyName(int d) {
    static const char* k[] = {"Easy", "Normal", "Hard", "Competitive", "A.I.", "Rising", "Mixed"};
    return d >= 0 && d < 7 ? k[d] : "Hard";
}
const char* galaxyName(int g) {
    static const char* k[] = {"Random", "Draft", "Seeded", "Your choice"};
    return g >= 0 && g < 4 ? k[g] : "Draft";
}
bool listHas(const char* list, const std::string& key) {
    if (!list || key.empty()) return false;
    const char* p = list;
    while (*p) {
        const char* e = std::strchr(p, ',');
        const size_t n = e ? size_t(e - p) : std::strlen(p);
        if (n == key.size() && std::strncmp(p, key.c_str(), n) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

}  // namespace league
