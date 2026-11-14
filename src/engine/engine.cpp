#include "engine/engine.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "core/log.h"
#include "engine/rules.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ti {

namespace {
struct AbortGame {};
#ifndef _WIN32
constexpr size_t kStackSize = 1024 * 1024;
#endif
const char* kSaveMagic = "TI4MIYOO-SAVE";
constexpr int kSaveFormat = 14;  // 1 = v0.2.x, 2 = v0.3.x, 3 = v0.4.0, 4 = v0.4.1, 5 = v0.5.0, 6 = v0.5.1, 7 = v0.6.0, 8 = v0.7.0, 9 = v0.8.0, 10 = v0.8.1, 11 = v0.9.0, 12 = v0.10.0, 13 = v0.11.0, 14 = v0.12.0
}  // namespace

// --------------------------------------------------------------- answers
std::string Answer::encode() const {
    std::string s;
    auto list = [&](const std::vector<int>& v) {
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ',';
            s += std::to_string(v[i]);
        }
    };
    list(picks);
    s += '|';
    s += std::to_string(number);
    s += '|';
    list(data);
    return s;
}

bool Answer::decode(const std::string& s, Answer& out) {
    out = Answer();
    size_t a = s.find('|');
    if (a == std::string::npos) return false;
    size_t b = s.find('|', a + 1);
    if (b == std::string::npos) return false;
    auto parse = [](const std::string& part, std::vector<int>& v) {
        size_t i = 0;
        while (i < part.size()) {
            size_t j = part.find(',', i);
            if (j == std::string::npos) j = part.size();
            if (j > i) v.push_back(std::atoi(part.substr(i, j - i).c_str()));
            i = j + 1;
        }
    };
    parse(s.substr(0, a), out.picks);
    out.number = std::atoi(s.substr(a + 1, b - a - 1).c_str());
    parse(s.substr(b + 1), out.data);
    return true;
}

// --------------------------------------------------------------- engine
Engine::Engine() = default;

Engine::~Engine() {
    if (started_ && !finished_) {
        // Unwind the suspended game so its objects are destroyed cleanly.
        aborting_ = true;
        switchToGame();
    }
#ifdef _WIN32
    if (fiber_) DeleteFiber(fiber_);
#endif
}

#ifdef _WIN32
void __stdcall Engine::fiberProc(void* self) {
    Engine* e = static_cast<Engine*>(self);
    e->fiberMain();
    for (;;) SwitchToFiber(e->mainFiber_);  // a fiber must never return
}

void Engine::switchToGame() {
    if (!IsThreadAFiber()) ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    mainFiber_ = GetCurrentFiber();
    SwitchToFiber(fiber_);
}

void Engine::switchToMain() { SwitchToFiber(mainFiber_); }
#else
void Engine::trampoline(uint32_t lo, uint32_t hi) {
    uintptr_t p = uintptr_t(lo);
#if UINTPTR_MAX > 0xFFFFFFFFu
    p |= uintptr_t(hi) << 32;
#else
    (void)hi;
#endif
    reinterpret_cast<Engine*>(p)->fiberMain();
}

void Engine::switchToGame() { swapcontext(&mainCtx_, &fiberCtx_); }
void Engine::switchToMain() { swapcontext(&fiberCtx_, &mainCtx_); }
#endif

void Engine::fiberMain() {
    try {
        Rules rules(*this);
        rules.playGame();
    } catch (const AbortGame&) {
    } catch (const std::exception& e) {
        crash_ = e.what();
        LOG("engine: exception: %s", e.what());
    } catch (...) {
        crash_ = "unknown error";
        LOG("engine: unknown exception");
    }
    finished_ = true;
    hasPending_ = false;
    // returning switches back to the main code (uc_link / fiberProc)
}

void Engine::start(const Setup& s) {
    if (started_ && !finished_) {  // abandon a game in progress
        aborting_ = true;
        switchToGame();
        aborting_ = false;
    }
    setup_ = s;
    crash_.clear();
    hasPending_ = false;
    g_ = Game();
    rng_.reseed(s.seed);
    answers_.clear();
    asks_.clear();
    events_.clear();
    reveals_ = 0;
    started_ = true;
    finished_ = false;
#ifdef _WIN32
    if (fiber_) DeleteFiber(fiber_);
    // 8 MB of address space for the game's stack; Windows commits it as it grows.
    fiber_ = CreateFiberEx(64 * 1024, 8 * 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, &Engine::fiberProc, this);
    if (!fiber_) {
        crash_ = "could not create the game's fiber";
        LOG("engine: CreateFiberEx failed (%lu)", (unsigned long)GetLastError());
        finished_ = true;
        return;
    }
#else
    stack_.assign(kStackSize, 0);
    getcontext(&fiberCtx_);
    fiberCtx_.uc_stack.ss_sp = stack_.data();
    fiberCtx_.uc_stack.ss_size = stack_.size();
    fiberCtx_.uc_link = &mainCtx_;
    uintptr_t p = reinterpret_cast<uintptr_t>(this);
    uint32_t lo = uint32_t(p & 0xFFFFFFFFu);
#if UINTPTR_MAX > 0xFFFFFFFFu
    uint32_t hi = uint32_t(p >> 32);
#else
    uint32_t hi = 0;
#endif
    makecontext(&fiberCtx_, reinterpret_cast<void (*)()>(&Engine::trampoline), 2, lo, hi);
#endif
    resume();
}

void Engine::resume() { switchToGame(); }

Answer Engine::ask(Decision d) {
    if (beforeAsk) beforeAsk();
    // Record who is asked and how much randomness / hidden information has
    // been used so far; undo compares these between decisions.
    if (asks_.size() == answers_.size()) asks_.push_back({int8_t(d.player), rng_.calls(), reveals_});
    pending_ = std::move(d);
    hasPending_ = true;
    switchToMain();
    if (aborting_) throw AbortGame();
    return current_;
}

bool Engine::answer(const Answer& in, std::string* err) {
    if (!hasPending_ || finished_) {
        if (err) *err = "no question is waiting";
        return false;
    }
    Answer a = in;
    // 0.52.0: one more unit type (the second flagship, last) - a production answer from an older save is one number short
    if (pending_.kind == DecisionKind::Produce && a.data.size() == size_t(kUnitTypes))
        a.data.insert(a.data.begin() + (kUnitTypes - 1), 0);
    if (pending_.validate) {
        std::string e = pending_.validate(a);
        if (!e.empty()) {
            if (err) *err = e;
            return false;
        }
    }
    answers_.push_back(a);
    current_ = a;
    hasPending_ = false;
    resume();
    return true;
}

void Engine::emit(Event e) {
    // The 0.16.0 "shown large" events stay out of the game log: the log's length
    // paces checkUnlocks, and old saves must replay exactly as before.
    const bool display = e.kind == EventKind::ObjectiveScored || e.kind == EventKind::RelicUsed || e.kind == EventKind::ComponentUsed ||
                         e.kind == EventKind::Planetfall || e.kind == EventKind::CardDrawn || e.kind == EventKind::ObjectiveRevealed ||
                         e.kind == EventKind::Note;
    if (e.kind != EventKind::Dice && !display && !e.text.empty()) g_.log.push_back(e.text);
    events_.push_back(std::move(e));
}

void Engine::log(int player, const std::string& text, EventKind kind, int system) {
    Event e;
    e.kind = kind;
    e.player = int8_t(player);
    e.system = int16_t(system);
    e.text = text;
    emit(std::move(e));
}

bool Engine::canUndo() const {
    if (!hasPending_ || finished_ || answers_.empty() || asks_.size() != answers_.size() + 1) return false;
    const AskRecord& cur = asks_[answers_.size()];
    const AskRecord& prev = asks_[answers_.size() - 1];
    return prev.player == cur.player && prev.rng == cur.rng && prev.reveals == cur.reveals;
}

int Engine::undoDepth(const std::function<bool(int)>& computer) const {
    if (!hasPending_ || finished_ || answers_.empty() || asks_.size() != answers_.size() + 1) return 0;
    const AskRecord& cur = asks_[answers_.size()];
    for (size_t i = answers_.size(); i-- > 0;) {
        const AskRecord& prev = asks_[i];
        if (prev.rng != cur.rng || prev.reveals != cur.reveals) return 0;
        if (prev.player == cur.player) return int(answers_.size() - i);
        if (!computer(prev.player)) return 0;
    }
    return 0;
}

bool Engine::undoSteps(int n) {
    if (n <= 0 || size_t(n) > answers_.size()) return false;
    std::vector<Answer> keep(answers_.begin(), answers_.end() - n);
    std::string err;
    bool ok = replay(keep, &err);
    if (!ok) LOG("engine: undo replay failed: %s", err.c_str());
    return ok;
}

bool Engine::replay(const std::vector<Answer>& answers, std::string* err) {
    Setup s = setup_;
    start(s);
    for (size_t i = 0; i < answers.size(); ++i) {
        std::string e;
        if (!answer(answers[i], &e)) {
            if (err) *err = "step " + std::to_string(i + 1) + ": " + e;
            return false;
        }
    }
    events_.clear();
    return true;
}

bool Engine::undo() {
    if (!canUndo()) return false;
    std::vector<Answer> keep(answers_.begin(), answers_.end() - 1);
    std::string err;
    bool ok = replay(keep, &err);
    if (!ok) LOG("engine: undo replay failed: %s", err.c_str());
    return ok;
}

std::vector<Event> Engine::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

// --------------------------------------------------------------- save / load
std::string Engine::saveText() const {
    std::ostringstream o;
    o << kSaveMagic << " " << kSaveFormat << "\n";
    o << "players " << setup_.players << "\n";
    o << "human " << setup_.human << "\n";
    o << "seed " << setup_.seed << "\n";
    o << "vp " << setup_.vpGoal << "\n";
    o << "color " << setup_.humanColor << "\n";
    o << "ai " << setup_.aiLevel << "\n";
    o << "auto " << (setup_.autoplay ? 1 : 0) << "\n";
    o << "trade " << setup_.trade << "\n";
    o << "draft " << setup_.draft << "\n";
    if (setup_.mapSeed) o << "mapseed " << setup_.mapSeed << "\n";
    if (setup_.layout) o << "layout " << setup_.layout << "\n";
    o << "extras " << setup_.extras << "\n";
    if (setup_.modules) o << "modules " << setup_.modules << "\n";
    if (setup_.events) o << "events " << setup_.events << "\n";
    if (setup_.modules & kModDiscordant) o << "dslevel " << setup_.dsLevel << "\n";
    if (setup_.fixes) o << "fixes " << setup_.fixes << "\n";
    o << "factions";
    for (int f : setup_.factions) o << ' ' << f;
    o << "\n";
    o << "arch";
    for (int a : setup_.archetypes) o << ' ' << a;
    o << "\n";
    if (!setup_.colors.empty()) {
        o << "colors";
        for (int c : setup_.colors) o << ' ' << c;
        o << "\n";
    }
    if (!setup_.levels.empty()) {
        o << "levels";
        for (int l : setup_.levels) o << ' ' << l;
        o << "\n";
    }
    if (setup_.humans) o << "humans " << setup_.humans << "\n";
    if (setup_.habits.games > 0) {
        const HabitProfile& h = setup_.habits;
        o << "habits " << h.games << ' ' << h.attacks << ' ' << h.raids << ' ' << h.mecatol << ' ' << h.broken << ' ' << h.kept << ' '
          << h.deals << "\n";
    }
    bool anyTribunii = false;
    for (int t : setup_.tribunii) anyTribunii |= t >= 0;
    if (anyTribunii) {
        o << "tribunii";
        for (int t : setup_.tribunii) o << ' ' << t;
        o << "\n";
    }
    o << "answers " << answers_.size() << "\n";
    for (const Answer& a : answers_) o << a.encode() << "\n";
    return o.str();
}

bool Engine::loadText(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string word;
    int version = 0;
    in >> word >> version;
    if (word != kSaveMagic) {
        if (err) *err = "not a saved game";
        return false;
    }
    if (version != kSaveFormat) {
        if (err) *err = "this saved game was made by an older version of the game and can't be continued - please start a new game";
        return false;
    }
    Setup s;
    s.factions.clear();
    s.trade = 0;  // older saves: the old Trade card rules
    s.draft = 0;  // ...and the random galaxy
    s.extras = 0; // ...and no extra rules (Kingslay)
    s.modules = 0; // ...and no modules (0.31.0)
    s.fixes = 0;   // ...and the rules as they were (0.33.0)
    size_t count = 0;
    while (in >> word) {
        if (word == "players") in >> s.players;
        else if (word == "human") in >> s.human;
        else if (word == "seed") in >> s.seed;
        else if (word == "vp") in >> s.vpGoal;
        else if (word == "color") in >> s.humanColor;
        else if (word == "ai") in >> s.aiLevel;
        else if (word == "trade") in >> s.trade;
        else if (word == "draft") in >> s.draft;
        else if (word == "mapseed") in >> s.mapSeed;
        else if (word == "layout") in >> s.layout;
        else if (word == "extras") in >> s.extras;
        else if (word == "modules") in >> s.modules;
        else if (word == "events") in >> s.events;
        else if (word == "dslevel") in >> s.dsLevel;
        else if (word == "fixes") in >> s.fixes;
        else if (word == "auto") {
            int v = 0;
            in >> v;
            s.autoplay = v != 0;
        }
        else if (word == "factions") {
            std::string line;
            std::getline(in, line);
            std::istringstream ls(line);
            int f;
            while (ls >> f) s.factions.push_back(f);
        } else if (word == "arch") {
            std::string line;
            std::getline(in, line);
            std::istringstream ls(line);
            int a;
            while (ls >> a) s.archetypes.push_back(a);
        } else if (word == "colors" || word == "levels" || word == "tribunii") {
            std::vector<int>& v = word == "colors" ? s.colors : word == "levels" ? s.levels : s.tribunii;
            std::string line;
            std::getline(in, line);
            std::istringstream ls(line);
            int a;
            while (ls >> a) v.push_back(a);
        } else if (word == "humans") {
            in >> s.humans;
        } else if (word == "habits") {
            HabitProfile& h = s.habits;
            in >> h.games >> h.attacks >> h.raids >> h.mecatol >> h.broken >> h.kept >> h.deals;
        } else if (word == "answers") {
            in >> count;
            std::getline(in, word);
            break;
        }
    }
    std::vector<Answer> list;
    std::string line;
    for (size_t i = 0; i < count; ++i) {
        if (!std::getline(in, line)) {
            if (err) *err = "save file is cut short";
            return false;
        }
        Answer a;
        if (!Answer::decode(line, a)) {
            if (err) *err = "damaged save file";
            return false;
        }
        list.push_back(a);
    }
    setup_ = s;
    std::string e;
    if (!replay(list, &e)) {
        if (err) *err = "save doesn't match this version of the game (" + e + ")";
        return false;
    }
    events_.clear();
    return true;
}

}  // namespace ti
