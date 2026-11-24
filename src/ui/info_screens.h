// Full-screen reference screens opened from the in-game menu: the faction
// sheet (command tokens, leaders...), technology cards, planet cards and unit
// cards. They only read the game state.
#pragma once
#include <cstdint>
#include <memory>

#include "core/input.h"
#include "engine/state.h"
#include "gfx/canvas.h"

class InfoScreen {
public:
    virtual ~InfoScreen() = default;
    // Returns false when the screen should close.
    virtual bool update(const Input& in) = 0;
    virtual void draw(Canvas& c, uint32_t now) = 0;
};

// start: the player shown first (-1: you); objectivesPage: open on secrets + technology (X toggles).
std::unique_ptr<InfoScreen> makeFactionSheet(const ti::Game& g, int human, int start = -1, bool objectivesPage = false);
std::unique_ptr<InfoScreen> makeTechScreen(const ti::Game& g, int human);
// 0.28.0: choosing research from the technology cards. pick() returns the tech
// confirmed (A on the opened card) or -2; B / MENU are left to the caller.
class ResearchPick {
public:
    virtual ~ResearchPick() = default;
    virtual int pick(const Input& in) = 0;
    virtual void paint(Canvas& c, uint32_t now) = 0;
    virtual bool detailOpen() const = 0;
};
std::unique_ptr<ResearchPick> makeResearchPick(const ti::Game& g, int player, const std::vector<int>& techs,
                                               const std::vector<std::string>& notes, const std::string& prompt);
std::unique_ptr<InfoScreen> makePlanetScreen(const ti::Game& g, int human);
std::unique_ptr<InfoScreen> makeUnitScreen(const ti::Game& g, int human);
std::unique_ptr<InfoScreen> makeHandScreen(const ti::Game& g, int human);  // action cards + promissory notes
std::unique_ptr<InfoScreen> makeLawScreen(const ti::Game& g, int human);   // laws in play, all agendas
std::unique_ptr<InfoScreen> makeMonumentScreen(const ti::Game& g, int human);  // (0.82.0) every player's monument (Monuments+)

// Colours shared with the game screen.
uint32_t strategyCardColor(int card);
uint32_t techColor(int tech);
