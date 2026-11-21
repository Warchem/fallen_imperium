// Negotiations screens (0.11.0): the faction web, the deal composer (offers
// and counter-offers), the answer to an offer, and the relations panel the
// in-game menu shows for a faction picked on the web.
#pragma once
#include <memory>
#include <vector>

#include "engine/state.h"
#include "gfx/canvas.h"
#include "ui/decision_ui.h"
#include "ui/map_view.h"

// DecisionKind::Deal - compose an offer or a counter-offer.
std::unique_ptr<DecisionUI> makeDealUI(const ti::Game& g, const ti::Decision& d, MapView& map);
// (0.64.0) the offer screen's opponent: 2 = a band above the rows - its portrait and the line it greets you with (owner:
// "Offer B looks like the best one"); 1 = the draft he didn't choose (portrait and line in the right-hand panel); 0 = none.
int& dealLook();
// Tag::DealResponse - accept, decline or counter an offer.
std::unique_ptr<DecisionUI> makeDealResponseUI(const ti::Game& g, const ti::Decision& d, MapView& map);
// Tag::Negotiate - choose who to make an offer to, on the faction web.
std::unique_ptr<DecisionUI> makeNegotiateUI(const ti::Game& g, const ti::Decision& d);

// The faction web: each player's emblem placed where its home system lies
// around Mecatol Rex (turned so the viewer sits at the bottom), joined by
// lines: grey (neighbours brighter), cyan = a promise, green = a deal in force
// (notes / contract promises), red = hostile (attacked or broke a promise).
struct FactionWeb {
    int n = 0;
    float x[8] = {}, y[8] = {};
    float cx = 0, cy = 0;
};
FactionWeb factionWebLayout(const ti::Game& g, Rect area, int viewer);
// enabled (optional): players that can be chosen; the others are drawn dimmed.
void drawFactionWeb(Canvas& c, const ti::Game& g, const FactionWeb& w, int viewer, int selected, uint32_t now,
                    const std::vector<bool>* enabled = nullptr);
void drawWebLegend(Canvas& c, int x, int y);
// D-pad step on the web: the nearest allowed player in direction (dx, dy).
int webStep(const FactionWeb& w, int from, int dx, int dy, const std::vector<bool>& allowed);
// What 'me' and q have going on: trading, promises, past deals.
void drawRelationsPanel(Canvas& c, const ti::Game& g, Rect r, int me, int q);
