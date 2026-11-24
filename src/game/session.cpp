#include "game/session.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "ai/ai.h"
#include "audio/audio.h"
#include "core/log.h"
#include "platform/platform.h"

using ti::EventKind;

std::string& GameSession::savePath() {
    static std::string path = "savegame.ti4";
    return path;
}

bool GameSession::startNew(const ti::Setup& s) {
    think_.reset();
    feed_.clear();
    queue_.clear();
    viewer_ = -1;
    aiRng_.reseed(s.seed ^ 0xA11CEu);
    engine_.start(s);
    absorbEvents();
    LOG("session: new game, %d players, seed %u, crash='%s'", s.players, s.seed, engine_.crashMessage().c_str());
    save();
    return engine_.crashMessage().empty();
}

bool GameSession::saveExists() { return fileExists(savePath()); }
bool GameSession::fileExists(const std::string& path) {
    std::ifstream in(path);
    return in.good();
}

void GameSession::rebuildFeed() {
    feed_.clear();
    const auto& log = engine_.game().log;
    for (size_t i = log.size() > 40 ? log.size() - 40 : 0; i < log.size(); ++i) {
        FeedLine f;
        f.text = log[i];
        for (char& ch : f.text)
            if (ch == '\n') ch = ' ';  // (0.81.0: Absol's card texts have line breaks - one feed line)
        feed_.push_back(f);
    }
}

bool GameSession::loadSaved(std::string* err) {
    think_.reset();
    std::ifstream in(path());
    if (!in) {
        if (err) *err = "no saved game";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    queue_.clear();
    viewer_ = -1;
    bool ok = engine_.loadText(ss.str(), err);
    // AI randomness continues from a fresh seed; AI choices are already in the log.
    aiRng_.reseed(engine_.setup().seed ^ uint32_t(engine_.answerCount()) ^ 0x5EEDu);
    engine_.takeEvents();
    rebuildFeed();
    LOG("session: loaded save (%d answers): %s", engine_.answerCount(), ok ? "ok" : (err ? err->c_str() : "?"));
    return ok;
}

bool GameSession::save() {
    if (!engine_.started()) return false;
    std::string tmp = path() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out) return false;
        out << engine_.saveText();
        if (!out) return false;
    }
    if (!replaceFile(tmp, path())) {
        LOG("session: could not replace %s", path().c_str());
        return false;
    }
    unsavedAnswers_ = 0;
    return true;
}

void GameSession::deleteSave() { std::remove(path().c_str()); }

bool GameSession::humanToAnswer() const {
    return engine_.waiting() && engine_.decision().player == human() && human() >= 0 && queue_.empty() && !holding_;
}

int GameSession::passTo() const {
    if (!engine_.waiting() || !queue_.empty() || holding_) return -1;
    const int p = engine_.decision().player;
    return isHumanSeat(p) && p != human() ? p : -1;
}

int GameSession::undoDepth() const {
    if (!humanToAnswer()) return 0;
    if (engine_.canUndo()) return 1;
    return engine_.undoDepth([this](int q) { return q >= 0 && !isHumanSeat(q); });  // back past the bots' answers
}

bool GameSession::canUndo() const { return undoDepth() > 0; }

bool GameSession::undo() {
    think_.reset();
    const int n = undoDepth();
    if (n <= 0) return false;
    bool ok = engine_.undoSteps(n);
    queue_.clear();
    engine_.takeEvents();
    rebuildFeed();
    FeedLine f;
    f.text = "Undone: back one step.";
    f.kind = EventKind::Warning;
    feed_.push_back(f);
    save();
    return ok;
}

void GameSession::absorbEvents() {
    for (ti::Event& e : engine_.takeEvents()) queue_.push_back(std::move(e));
}

// How long each kind of event stays on screen at "Normal" speed (ms).
float GameSession::speedFactor() const {
    static const float kSpeed[4] = {1.6f, 1.0f, 0.45f, 0.0f};
    return kSpeed[speed_ < 0 ? 0 : speed_ > 3 ? 3 : speed_];
}

uint32_t GameSession::durationOf(const ti::Event& e) const {
    static const float kSpeed[4] = {1.6f, 1.0f, 0.45f, 0.0f};
    if (skipCombat_) return 0;  // START: the rest of the combat at once
    uint32_t ms = 250;
    switch (e.kind) {
        case EventKind::TurnStart: ms = 700; break;
        case EventKind::TurnEnd: ms = 1100; break;
        case EventKind::RoundEnd: ms = 1800; break;
        case EventKind::Phase: ms = 1200; break;
        case EventKind::StrategyPick: ms = 900; break;
        case EventKind::ActionChosen: ms = e.card == ti::kActionPass ? 300 : 2000; break;
        case EventKind::Activate: ms = e.hostile ? 2000 : 1400; break;
        case EventKind::Move: ms = 1100; break;
        case EventKind::Land: ms = 800; break;
        case EventKind::Produce: ms = 900; break;
        case EventKind::Secondary: ms = 1000; break;
        case EventKind::CombatStart: ms = 1600; break;
        case EventKind::Dice: ms = 1600; break;  // 0.22.0: the dice roll for about a second (owner)
        case EventKind::Damaged: ms = 500; break;
        case EventKind::Destroyed: ms = 600; break;
        case EventKind::CombatEnd: ms = 1500; break;
        case EventKind::Retreat: ms = 1200; break;
        case EventKind::Control: ms = 700; break;
        case EventKind::Research: ms = 1000; break;
        case EventKind::Score: ms = 1800; break;
        case EventKind::Eliminated: ms = 2500; break;
        case EventKind::Warning: ms = 800; break;
        case EventKind::Log: ms = e.system >= 0 ? 450 : 250; break;
        case EventKind::ActionCard: ms = 1800; break;
        case EventKind::Agenda: ms = 2600; break;
        case EventKind::Vote: ms = 900; break;
        case EventKind::AgendaResult: ms = 2600; break;
        case EventKind::Explore: ms = 2200; break;
        case EventKind::Relic: ms = 2600; break;
        case EventKind::Leader: ms = 2200; break;
        case EventKind::Ability: ms = 1800; break;
        case EventKind::AgendaPeek: ms = 3500; break;
        case EventKind::Reveal: ms = 5000; break;
        case EventKind::Betrayal: ms = 4500; break;
        case EventKind::TilePlaced: ms = 900; break;
        case EventKind::ObjectiveScored: ms = e.count >= 0 && ti::kObjectives[e.count].kind == 3 ? 2400 : 0; break;
        case EventKind::RelicUsed: ms = 2200; break;
        case EventKind::MonumentPlaced: ms = 3200; break;  // (0.82.0 Monuments+: its card)
        case EventKind::ComponentUsed: ms = 2200; break;
        case EventKind::Planetfall: ms = 6500; break;
        case EventKind::WarSunAge: ms = 17500; break;  // 0.73.0: the War Sun scene (timed: the whole of it)
        case EventKind::CardDrawn: ms = 0; break;
        case EventKind::ObjectiveRevealed: ms = 1500; break;
        case EventKind::Note: ms = e.system >= 0 ? 600 : 450; break;
        case EventKind::Deal: {
            // DealEventType (0.58.4: a broken promise is the betrayal codec - time to read both lines when timed)
            static const uint32_t kMs[6] = {1800, 2400, 1300, 7000, 1500, 1800};
            ms = e.card >= 0 && e.card < 6 ? kMs[e.card] : 1500;
            break;
        }
    }
    // The player's own choices need less time on screen (combat dice keep full time).
    if (isHumanSeat(e.player) &&
        (e.kind == EventKind::Activate || e.kind == EventKind::ActionChosen || e.kind == EventKind::Move ||
         e.kind == EventKind::Land || e.kind == EventKind::Produce || e.kind == EventKind::StrategyPick || e.kind == EventKind::Secondary || e.kind == EventKind::Research ||
         e.kind == EventKind::TilePlaced))
        ms /= 2;
    float k = kSpeed[speed_ < 0 ? 0 : speed_ > 3 ? 3 : speed_];
    if (e.kind == EventKind::WarSunAge && speed_ != 3) k = 1.f;  // (0.73.0: the scene has its own length at every speed but Instant)
    if (e.kind == EventKind::Ability && e.label == "The Fracture" && speed_ != 3) {  // (0.73.5: the map ripples out from Styx)
        ms = 3400;
        k = 1.f;
    }
    // (0.73.8: the Firmament becomes the Obsidian - the screams, then the ripple from their home)
    if (e.kind == EventKind::Ability && e.label == "Puppets of the Blade" && e.text.find("cast off their mask") != std::string::npos &&
        speed_ != 3) {
        ms = audio::kGalaxyScreamsMs + 3400;
        k = 1.f;
    }
    if (hurry_) k *= 0.15f;
    return uint32_t(float(ms) * k);
}

std::vector<ti::Event> GameSession::step(uint32_t now, bool hurry) {
    std::vector<ti::Event> started;
    hurry_ = hurry;
    for (int guard = 0; guard < 400; ++guard) {
        if (holding_) break;                        // a card waits for the player to press A
        if (int32_t(now - showUntil_) < 0) break;  // still showing the previous event
        if (!queue_.empty()) {
            ti::Event e = std::move(queue_.front());
            queue_.pop_front();
            showUntil_ = now + durationOf(e);
            // Combats (0.22.0): note who fights; after a space combat the screen waits for A so
            // the aftermath can be seen (MENU > Cards: all, only yours, or timed), unless skipped.
            if (e.kind == ti::EventKind::CombatStart) {
                inCombat_ = true;
                combatSpace_ = e.space;
                combatSides_[0] = e.player;
                combatSides_[1] = e.other;
            } else if (e.kind == ti::EventKind::CombatEnd || e.kind == ti::EventKind::TurnEnd || e.kind == ti::EventKind::Phase) {
                const bool mine = isHumanSeat(combatSides_[0]) || isHumanSeat(combatSides_[1]);
                if (e.kind == ti::EventKind::CombatEnd && inCombat_ && !skipCombat_ && speed_ != 3 &&
                    (cardPause_ == 0 || (cardPause_ == 1 && mine))) {
                    holding_ = true;
                    heldKind_ = e.kind;
                }
                inCombat_ = false;
                skipCombat_ = false;
            }
            // Cards wait on screen for A (MENU > Cards): exploration, relics, agenda peeks and,
            // since 0.16.0 (owner), action cards, secret objectives scored, relics and component
            // actions used; agendas and their rulings, the reveal and betrayals unless "timed".
            // secret objectives, and public ones too since 0.28.0 (owner)
            const bool secret = e.kind == ti::EventKind::ObjectiveScored && e.count >= 0;
            const bool card = e.kind == ti::EventKind::Explore || e.kind == ti::EventKind::Relic || e.kind == ti::EventKind::AgendaPeek ||
                              e.kind == ti::EventKind::ActionCard || e.kind == ti::EventKind::RelicUsed ||
                              e.kind == ti::EventKind::MonumentPlaced ||  // (0.82.0: owner - bots' ones follow the "Cards" setting too)
                              e.kind == ti::EventKind::ComponentUsed || secret ||
                              // (0.82.0, owner 2026-11-22 M18) a monument's bonus a person gets without a question (Sector KVD-14's
                              // trade goods, Palatine Obelisk's votes): its card, large, for that person
                              (e.kind == ti::EventKind::Ability && e.unit == int8_t(ti::UnitType::Monument) && e.count > 0 && isHumanSeat(e.player)) ||
                              // 0.28.0 (owner): leaders shown large when used, to read them
                              (e.kind == ti::EventKind::Leader && e.card >= 0 && e.label.find("unlocked") == std::string::npos);
            // 0.29.0 (owner): your own new action cards (only the drawing player sees them), new public objectives
            const bool drawnMine = e.kind == ti::EventKind::CardDrawn && e.player == human() && e.count >= 0;
            if (e.kind == ti::EventKind::CardDrawn && !drawnMine) {
                started.push_back(std::move(e));  // someone else's card stays hidden: nothing to show
                continue;
            }
            // 0.57.0: a promise broken to or by a person shows the betrayal codec - it waits like a betrayal
            // 0.58.4 (owner, L4: "do betrayal codecs for bot-to-bot betrayals as well"): every broken promise
            const bool brokenMine = e.kind == ti::EventKind::Deal && e.card == 3 /* kDealBroken */;
            const bool table = e.kind == ti::EventKind::Reveal || e.kind == ti::EventKind::Betrayal || e.kind == ti::EventKind::Agenda ||
                               e.kind == ti::EventKind::AgendaResult || e.kind == ti::EventKind::Planetfall ||
                               e.kind == ti::EventKind::WarSunAge || brokenMine;
            if (speed_ != 3 && ((card && (cardPause_ == 0 || (cardPause_ == 1 && isHumanSeat(e.player)))) ||
                                (table && cardPause_ != 2) || (drawnMine && cardPause_ != 2) ||
                                (e.kind == ti::EventKind::ObjectiveRevealed && cardPause_ != 2))) {
                holding_ = true;
                heldKind_ = e.kind;
            }
            if (!e.text.empty()) {
                FeedLine f;
                f.text = e.text;
                for (char& ch : f.text)
                    if (ch == '\n') ch = ' ';  // (0.81.0: Absol's card texts have line breaks - one feed line)
                f.player = e.player;
                f.kind = e.kind;
                f.time = now;
                f.system = e.system;
                feed_.push_back(f);
                while (feed_.size() > 300) feed_.pop_front();
            }
            started.push_back(std::move(e));
            continue;
        }
        if (!engine_.waiting() || isHumanSeat(engine_.decision().player)) {  // a person answers (hot-seat: any of them)
            if (engine_.waiting())
                logx::crumb("round %d, your question (tag %d): %.120s", engine_.game().round, int(engine_.decision().tag),
                            engine_.decision().prompt.c_str());
            break;
        }
        // The computer players act.
        const ti::Decision& d = engine_.decision();
        logx::crumb("round %d, answer %d, bot %d (tag %d): %.120s", engine_.game().round, engine_.answerCount(), d.player,
                    int(d.tag), d.prompt.c_str());
        // (0.74.0) an A.I. seat plays its options out a little each frame, so the screen keeps moving meanwhile
        if (think_ && think_->stale(engine_)) think_.reset();
        if (!think_) {
            std::unique_ptr<ti::AiThinkJob> job(new ti::AiThinkJob());
            if (job->begin(engine_, aiRng_)) think_ = std::move(job);
        }
        ti::Answer a;
        if (think_) {
            if (!think_->step(speed_ == 3 ? 40 : 14)) break;  // more next frame
            a = think_->answer();
            think_.reset();
        } else {
            a = ti::aiAnswer(engine_.game(), d, aiRng_);
        }
        std::string err;
        if (!engine_.answer(a, &err)) {
            LOG("session: AI answer rejected (%s): %s", d.prompt.c_str(), err.c_str());
            ti::Answer f;
            if (d.kind == ti::DecisionKind::Pick) {
                for (const ti::Option& o : d.options)
                    if (o.enabled && int(f.picks.size()) < std::max(1, d.minPick)) f.picks.push_back(o.id);
            } else if (d.kind == ti::DecisionKind::Produce) {
                f.data.assign(ti::kUnitTypes + 1, 0);
                f.data[ti::kUnitTypes] = -1;
            } else if (d.kind == ti::DecisionKind::Tokens) {
                const ti::Player& p = engine_.game().players[d.player];
                f.data = {p.tactic + d.newTokens, p.fleet, p.strategy};
            }
            if (!engine_.answer(f, &err)) {
                LOG("session: fallback rejected too: %s", err.c_str());
                break;
            }
        }
        ++unsavedAnswers_;
        absorbEvents();
        if (unsavedAnswers_ > 120) save();
    }
    if (engine_.finished() && unsavedAnswers_ > 0) save();
    return started;
}

bool GameSession::submit(const ti::Answer& a, std::string* err, uint32_t now) {
    (void)now;
    if (!engine_.answer(a, err)) return false;
    absorbEvents();
    ++unsavedAnswers_;
    save();
    return true;
}
