// A game in progress as seen by the app: owns the rules engine, lets the AI
// players act at a watchable pace, and saves the game automatically.
//
// Everything the engine reports (moves, combat dice, scores...) goes into a
// queue and is "presented" one event at a time, each for a set duration, so
// the player can follow what the computer players are doing. The engine only
// moves on when the queue is empty.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "engine/engine.h"

struct FeedLine {
    std::string text;
    int player = -1;
    ti::EventKind kind = ti::EventKind::Log;
    uint32_t time = 0;
    int system = -1;
};

class GameSession {
public:
    static std::string& savePath();  // defaults to "savegame.ti4" in the app folder

    bool startNew(const ti::Setup& s);
    bool loadSaved(std::string* err);
    static bool saveExists();
    bool save();
    void deleteSave();

    ti::Engine& engine() { return engine_; }
    const ti::Game& game() const { return engine_.game(); }
    // The seat the player answers for (-1: none - an all-computer game, or watching).
    // Hot-seat (0.19.0): the human seat now at the device (see passTo / takeSeat).
    int human() const { return engine_.setup().autoplay ? -1 : viewer(); }
    // The player's seat (whose view the screen shows), also when the computer plays it (watching).
    int seat() const { return viewer(); }
    // Is this seat played by a person (one of possibly several, hot-seat)?
    int undoDepth() const;  // answers an undo drops (0 = can't)
    bool isHumanSeat(int p) const { return !engine_.setup().autoplay && engine_.setup().isHuman(p); }
    // Hot-seat: a decision waits for another person's seat - show "Pass to ..." first
    // (returns that seat, or -1); takeSeat() hands the device over.
    int passTo() const;
    void takeSeat(int p) { viewer_ = p; }
    bool watching() const { return engine_.setup().autoplay && engine_.setup().human >= 0; }
    // A decision is waiting for the human and everything before it has been shown.
    bool humanToAnswer() const;
    bool over() const { return engine_.finished() && queue_.empty(); }

    // Presents queued events and lets AI players act. Returns the events whose
    // presentation started during this call (the screen reacts to them).
    std::vector<ti::Event> step(uint32_t now, bool hurry);
    bool submit(const ti::Answer& a, std::string* err, uint32_t now);
    bool canUndo() const;
    bool undo();

    // 0 slow, 1 normal, 2 fast, 3 instant
    void setSpeed(int speed) { speed_ = speed; }
    int speed() const { return speed_; }
    // Exploration and relic cards: 0 = every one waits for A, 1 = only the
    // human's own, 2 = shown for a set time like everything else.
    void setCardPause(int mode) { cardPause_ = mode; }
    // A card is on screen waiting for the player to press A (release()).
    bool holding() const { return holding_; }
    ti::EventKind heldKind() const { return heldKind_; }  // what kind of card is held
    void release() { holding_ = false; }
    const std::deque<FeedLine>& feed() const { return feed_; }
    bool presenting(uint32_t now) const { return holding_ || !queue_.empty() || int32_t(now - showUntil_) < 0; }
    // How long an event stays on screen at the current speed (ms).
    uint32_t durationOf(const ti::Event& e) const;
    // The speed setting as a time factor (1.6 slow, 1 normal, 0.45 fast, 0 instant).
    float speedFactor() const;
    // 0.58.4: the screen needs the event it is showing for longer (a bot's spoken line to read) - not while A
    // hurries the game along.
    void extendShow(uint32_t until) {
        if (!hurry_ && int32_t(until - showUntil_) > 0) showUntil_ = until;
    }
    // START during a combat (0.22.0): the rest of it is shown at once.
    void skipCombat() {
        if (inCombat_) skipCombat_ = true;
    }
    bool skippingCombat() const { return skipCombat_; }

private:
    void absorbEvents();
    void rebuildFeed();
    int viewer() const { return viewer_ >= 0 ? viewer_ : engine_.setup().human; }
    int viewer_ = -1;  // hot-seat: the human seat at the device (-1 = the primary seat)

    ti::Engine engine_;
    ti::Rng aiRng_{1};
    std::deque<ti::Event> queue_;
    std::deque<FeedLine> feed_;
    uint32_t showUntil_ = 0;
    bool hurry_ = false;
    int speed_ = 1;
    int cardPause_ = 0;
    bool holding_ = false;
    ti::EventKind heldKind_ = ti::EventKind::Log;
    // a combat being shown: after it the screen waits for A (like the cards), unless skipped
    bool inCombat_ = false, combatSpace_ = false, skipCombat_ = false;
    int combatSides_[2] = {-1, -1};
    int unsavedAnswers_ = 0;
};
