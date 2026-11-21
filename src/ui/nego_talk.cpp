// The opponent's opening line on your offer screen - see nego_talk.h. The lines are in tools/lines/about_*.txt (every
// faction, two for each kind of moment), with the rest of the negotiation lines (speech::pick).
#include "ui/nego_talk.h"

#include <algorithm>

#include "engine/data.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"
#include "ui/speech.h"

using namespace ti;

namespace negotalk {

const char* whyName(Why w) {
    static const char* kNames[] = {"hello", "neighbour", "partner", "kept", "broken", "clash", "leader", "friendly", "wary", "hostile"};
    return kNames[std::clamp(int(w), 0, int(Why::Count) - 1)];
}

Mood moodOf(const Game& g, int bot, int you) {
    Mood m;
    if (bot < 0 || you < 0 || bot >= g.numPlayers || you >= g.numPlayers) return m;
    const Personality& pe = g.players[size_t(bot)].pers;
    const int broke = pe.broke[size_t(you)], fought = pe.attacks[size_t(you)], kept = pe.kept[size_t(you)], deals = pe.deals[size_t(you)];
    int best = 0;
    for (int q = 0; q < g.numPlayers; ++q)
        if (q != you) best = std::max(best, int(g.players[size_t(q)].vp));
    const int lead = int(g.players[size_t(you)].vp) - best;
    const bool near = areNeighbours(g, bot, you);
    // the line: the sharpest thing first
    if (broke > 0) m.why = Why::Broken;
    else if (fought > 0) m.why = Why::Clash;
    else if (lead >= 2 && g.players[size_t(you)].vp >= 4) m.why = Why::Leader;
    else if (kept > 0) m.why = Why::Kept;
    else if (deals > 0) m.why = Why::Partner;
    else if (near) m.why = Why::Neighbour;
    else if (archetypesRevealed(g) && pe.arch >= 0) {
        const Attitude a = attitudeOf(g, bot, you);
        m.why = a == Attitude::Friendly ? Why::Friendly : a == Attitude::Hostile ? Why::Hostile : Why::Wary;
    }
    // the tags: what lies between you
    if (near) m.tags.push_back("Neighbours");
    if (broke > 0) m.tags.push_back(broke > 1 ? "You broke " + std::to_string(broke) + " promises" : std::string("You broke a promise"));
    if (fought > 0) m.tags.push_back(fought > 1 ? "You attacked them " + std::to_string(fought) + " times" : std::string("You attacked them"));
    if (kept > 0) m.tags.push_back(kept > 1 ? "You kept " + std::to_string(kept) + " promises" : std::string("You kept a promise"));
    if (deals > 0) m.tags.push_back(deals > 1 ? std::to_string(deals) + " deals made" : std::string("1 deal made"));
    if (lead >= 2 && g.players[size_t(you)].vp >= 4) m.tags.push_back("You lead by " + std::to_string(lead) + " VP");
    if (archetypesRevealed(g) && pe.arch >= 0) m.tags.push_back(attitudeName(attitudeOf(g, bot, you)));
    return m;
}

std::string quip(const Game& g, int bot, int you, Why why, uint32_t seed) {
    if (bot < 0 || you < 0 || bot >= g.numPlayers || you >= g.numPlayers) return "";
    return speech::pick(g.players[size_t(bot)].faction, g.players[size_t(you)].faction, speech::Cat(int(speech::Cat::Hello) + int(why)), seed);
}

}  // namespace negotalk
