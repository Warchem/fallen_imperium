// Negotiations (Stage 6): deals between players. A deal hands components
// over now (a transaction, LRR 94: trade goods, commodities, one promissory
// note per side, relic fragments, and for the Hacan action cards) and/or
// records promises about later (LRR 28: never binding - the game only notes
// whether they are kept). These helpers are shared by the rules, the AI and
// the screens.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "engine/state.h"

namespace ti {

// Decision::dealFlags: what the deal being composed may contain.
enum DealFlags {
    kDealComponents = 1,   // a transaction is allowed (neighbours / agenda phase / Guild Ships)
    kDealActionCards = 2,  // action cards may change hands (Hacan Arbiters)
    kDealVote = 4,         // agenda phase: "vote for an outcome" promises
    kDealCounter = 8,      // this is a counter-offer
    kDealBlackMarket = 16, // Black Market Dealings (Thunder's Edge): relics, action cards and secrets too
    kDealHire = 32,        // Hired Guns (Discordant Stars, 0.43.0): the Nokar may hire out ships (Decision::value = how many)
    kDealHub = 64,         // Encrypted Trade Hub (Edyn technology, 0.44.5): 1 relic may change hands
    kDealRelics = 128,     // (0.82.0) Quantum Reliquary (Naaz-Rokha monument): relics may change hands
    kDealUnits = 256,      // (0.82.0) Mowshir Freeport (Hacan monument): units in space areas near it may change hands
};
bool blackMarketPair(const Game& g, int a, int b);  // Black Market Dealings was played between them
// (0.82.0) Monuments+: Quantum Reliquary - a or b holds it (on the board) and the other controls units in or next to its system
bool reliquaryPair(const Game& g, int a, int b);
// Mowshir Freeport - the systems in or next to a Hacan monument on the board (their space areas' units may be traded)
std::vector<int> freeportSystems(const Game& g);
int encryptedHubOf(const Game& g, int a, int b);    // (0.44.5) a or b with a ready Encrypted Trade Hub, else -1
std::vector<int> lawsHeldBy(const Game& g, int p);  // (fixes 28) agendas (laws) p holds - tradeable with Encrypted Trade Hub

std::vector<int> encodeDeal(const Deal& d);
bool decodeDeal(const std::vector<int>& v, Deal& d);

// "Sol gives 2 trade goods and Ceasefire. Hacan gives 3 commodities. Sol promises to ..."
std::string dealText(const Game& g, const Deal& d);
std::string sideText(const Game& g, const DealSide& s);         // "2 trade goods, Ceasefire" ("nothing")
std::string promiseText(const Game& g, const Promise& p, bool withWho = true);
std::string promiseKindName(PromiseKind k);

// Can 'a' and 'b' exchange components right now (LRR 94)? During the action
// phase one of them must be the active player and they must be neighbours
// (Hacan: Guild Ships / Trade Convoys lift that); during the agenda phase
// anyone can. One transaction per pair per turn / agenda.
bool transactionAllowed(const Game& g, int a, int b);
bool actionCardsTradeable(const Game& g, int a, int b);  // Hacan Arbiters
// "" if the deal can be offered now; otherwise why not.
std::string checkDeal(const Game& g, const Deal& d, bool counter = false);
// Who may still receive an offer from p now (offer limit: 2 per pair per turn / agenda).
bool canOffer(const Game& g, int p, int q);
int offersLeft(const Game& g, int p, int q);

// Line between two players on the faction web.
enum class Relation : uint8_t { None, Promise, Contract, Hostile };
Relation relationOf(const Game& g, int a, int b);
bool hostileFlag(const Game& g, int a, int b);
// Active promises between a and b (either direction).
std::vector<const Promise*> activePromises(const Game& g, int a, int b);
// Rounds promised -> last round it applies.
int promiseLastRound(const Game& g, const Promise& p);

// Default trade value of components in trade goods (the owner's price list).
float noteDefaultValue(const Game& g, int note);
// Mecatol Rex / the Wormhole Nexus: systems "Leave ..." can be asked for.
bool leaveRequestable(const Game& g, int by, int sys);
// Is it public knowledge that this note is no longer in its owner's hand
// (face up in a play area, or handed over in a deal)?
bool noteKnownAway(const Game& g, int note);
// Who is publicly known to hold a note (its owner, or whoever got it in a
// deal); -1 when it is face up in a play area.
int knownHolder(const Game& g, int note);
// Notes q can be asked for: its own and those it got in deals, as far as anyone knows.
std::vector<int> askableNotes(const Game& g, int q);

// Choices for the deal screens.
// Systems a promise of this kind by 'by' (to 'to') can name, best first.
std::vector<int> promiseSystems(const Game& g, PromiseKind k, int by, int to);
// Systems 'from' could threaten to attack if 'to' refuses (their units, within reach of from's ships).
std::vector<int> threatSystems(const Game& g, int from, int to);
// Outcomes of an agenda that votes can be promised for: {id, name}.
std::vector<std::pair<int, std::string>> voteOutcomes(const Game& g, int agenda);
// Does this kind of promise run for a number of rounds?
bool promiseHasRounds(PromiseKind k);
// Can p's agent be lent to another player (the agents whose windows support it)?
bool agentLendable(const Game& g, int p);
std::string agentCardText(const Game& g, int p);  // 0.43.0: "<name> (agent): <text>" for the deal screen
bool promiseHasSystem(PromiseKind k);

// Event::card for EventKind::Deal.
enum DealEventType { kDealOffered = 0, kDealMade = 1, kDealDeclined = 2, kDealBroken = 3, kDealFailed = 4, kDealCountered = 5 };

// Axis Order cards (Discordant Stars, rules_ds5.cpp): name and text of card 0-7
std::string axisOrderName(int i);
std::string axisOrderText(int i);

}  // namespace ti
