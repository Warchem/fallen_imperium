// Galaxy construction: places tiles using the standard Prophecy of Kings
// layouts for 3-8 players (with hyperlanes for 3, 4, 5, 7 and 8 players; with
// Game::layout 1 (0.58.0, "Hyperlanes: Off") the printed boards without them),
// fills each player's "slice" with a balanced random selection of tiles, and
// works out which systems are adjacent.
#pragma once
#include <string>
#include <vector>

#include "engine/rng.h"
#include "engine/state.h"

namespace ti {

Hex layoutPosToHex(int pos);  // ring*100 + index -> axial hex
// factionBySeat must already be resolved (no -1 entries).
// balanced (0.43.0, the seeded galaxy): more deals tried and a stricter score (resources, influence, legendary
// planets, wormholes, anomalies); sliceOwner (optional): for each system made, the seat whose slice it is (-1 none)
bool buildGalaxy(Game& g, Rng& rng, const std::vector<int>& factionBySeat, std::string* err, bool balanced = false,
                 std::vector<int>* sliceOwner = nullptr);
// The numbers of each seat's slice on a map built with sliceOwner (0.43.0: the seeded galaxy's breakdown)
struct SliceStats {
    int res = 0, inf = 0, optimal = 0, planets = 0, legendary = 0, skips = 0, wormholes = 0, anomalies = 0;
};
std::vector<SliceStats> sliceStats(const Game& g, const std::vector<int>& sliceOwner, int players);
void computeAdjacency(Game& g);
// (0.59.0) u and v touch on the board - side by side or joined through hyperlane tiles - without any wormhole
bool boardAdjacent(const Game& g, int u, int v);
// Where the Wormhole Nexus is drawn beside the board (far from every home).
Hex nexusSpot(int maxRing, const std::vector<Hex>& used, const std::vector<Hex>& homes);
// Board edges joined through a hyperlane tile (taking its rotation into account).
bool hyperlaneJoins(const SystemState& s, int edgeA, int edgeB);
// The seat whose home system is lowest on the screen (southernmost) in the
// layout for this many players - the human plays it so their home isn't
// hidden under the prompts at the top of the map. -1 if no layout.
int southernmostSeat(int players, int layout = 0);
// 0.58.0: trade goods a seat starts with because of where its home is on the printed board (five players without
// hyperlanes: +2 / +4 / +2, LRR setup step 6); 0 otherwise.
int layoutStartTg(const Game& g, int p);
// A rough measure of how good a tile is for its owner (used for balancing).
float tileValue(int systemDef);

// Uncharted Space (0.33.0): Star Charts and Azdel's Key put a new tile at the edge of the board.
// Empty spaces next to at least 2 systems (hyperlanes don't count), and the blue-backed tiles that are
// neither on the board nor purged. A spot travels in a decision as spotId (and back with spotHex).
std::vector<Hex> boardEdgeSpots(const Game& g);
std::vector<int> blueTilesLeft(const Game& g);
inline int spotId(Hex h) { return (h.q + 64) * 256 + (h.r + 64); }
inline Hex spotHex(int id) { return Hex{id / 256 - 64, id % 256 - 64}; }
int addBoardSystem(Game& g, int def, Hex pos);  // a new system (and its planets); adjacency not yet updated
// A module's tiles are only dealt / drawn with that module (Uncharted Space, Thunder's Edge).
bool systemInGame(const Game& g, int def);
void tePlacement(Game& g);  // Thunder's Edge (0.34.0): where the Fracture and the Thunder's Edge token go

// Galaxy setup draft (0.15.0, LRR setup step 6). startGalaxyDraft builds the
// fixed part of the map (Mecatol Rex, hyperlanes, the home systems, the Creuss
// home beside its gate, the wormhole nexus), records the open spaces in
// g.draft and deals each player 3 blue and 2 red tiles (the most even of a
// number of random deals). Rules::galaxyDraft then has the players place them.
bool startGalaxyDraft(Game& g, Rng& rng, const std::vector<int>& factionBySeat, std::string* err);
// Where player p may place a tile now, as (tile, draft cell) pairs: spaces of
// the innermost ring that still has open ones; anomalies may not touch, nor
// systems with the same wormhole, unless no placement avoids it.
std::vector<std::pair<int, int>> draftPlacements(const Game& g, int p);
// Would this tile in this cell break the placement rules (anomalies or the
// same wormhole next to each other)?
bool draftTouchesBadly(const Game& g, int def, int cell);
// Places tile def (from p's hand) in draft cell `cell`; returns the system index.
int placeDraftTile(Game& g, int p, int def, int cell);
// After the last tile: work out adjacency, close the draft.
void finishGalaxyDraft(Game& g);

}  // namespace ti
