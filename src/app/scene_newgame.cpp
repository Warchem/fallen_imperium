// New game setup: number of players, victory point goal, the round-1 trade
// house rule, galaxy draft, AI speed, and (0.19.0, owner) the Faction Menu:
// for every seat who plays it (you, another person at the same device -
// hot-seat - or the computer), its faction (or Random), colour, and for
// computer players their difficulty (Competitive by default) and personality.
#include <algorithm>
#include <cstdlib>
#include <array>
#include <cstring>
#include <memory>

#include "game/habits.h"
#include "app/app.h"
#include "audio/audio.h"
#include "ui/backdrops.h"
#include "ui/newgame_bg.h"
#include "ui/retro.h"
#include "core/log.h"
#include "engine/data.h"
#include "engine/map.h"
#include "engine/personality.h"
#include "game/session.h"
#include "gfx/font.h"
#include "ui/icons.h"
#include "ui/map_view.h"
#include "ui/theme.h"
#include "ui/transition.h"
#include "ui/hits.h"
#include "ui/widgets.h"
#include "ui/pcolors.h"
#include "game/profile.h"
#include "game/league_run.h"
#include "game/leagues.h"

using namespace ti;

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

namespace {

const char* kSpeedNames[4] = {"Slow", "Normal", "Fast", "Instant"};
// (0.74.0) A.I. = kAiLevelAI: experimental, no personality, plays only to win
constexpr int kLevelCount = 5;
const char* kLevels[kLevelCount] = {"Easy", "Normal", "Hard", "Competitive", "A.I."};
const char* kLevelInfo[kLevelCount] = {"Relaxed opponents.", "They plan for objectives.", "They play to win by rounds 6-8.",
                                       "Sharpest play; personality habits toned down.",
                                       "Experimental: no personality; it plays only to win."};
uint32_t levelColour(int l) { return l >= kAiLevelAI ? rgb(255, 112, 140) : l == kAiLevelCompetitive ? theme::gold : theme::text; }

// The faction picker (0.28.2, owner): every faction as its emblem with name and a line
// about it; Thunder's Edge and Discordant Stars (fan-made) shown greyed, "coming soon".
// 0.43.0: the summary, difficulty and style of every faction, from the owner's faction strategy guide
// (tools/gen_guides.py writes the table).
struct GuideBlurb {
    const char* key;
    const char* difficulty;  // BEGINNER-FRIENDLY, INTERMEDIATE, ADVANCED or EXPERT
    const char* tags;        // the guide's style tags, comma separated
    const char* text;
};
#include "app/gen/faction_blurbs.inc"
const GuideBlurb* guideBlurbOf(int f) {
    if (f < 0) return nullptr;
    for (const GuideBlurb& b : kGuideBlurbs)
        if (!std::strcmp(b.key, kFactions[f].key)) return &b;
    return nullptr;
}
const char* blurbOf(int f) {
    if (f < 0) return "A faction chosen at random when the game starts.";
    const GuideBlurb* b = guideBlurbOf(f);
    return b ? b->text : "";
}
struct SoonFaction {
    const char* key;
    const char* name;
};
const SoonFaction kThundersEdge[] = {{"lastbastion", "Last Bastion"},          {"ralnel", "The Ral Nel Consortium"},
                                     {"deepwrought", "The Deepwrought Scholarate"}, {"crimson", "The Crimson Rebellion"},
                                     {"obsidian", "The Firmament / The Obsidian"}};
const SoonFaction kDiscordant[] = {
    {"axis", "The Shipwrights of Axis"},     {"bentor", "The Bentor Conglomerate"},   {"kjalengard", "The Berserkers of Kjalengard"},
    {"celdauri", "The Celdauri Trade Confederation"}, {"cheiran", "The Cheiran Hordes"}, {"cymiae", "The Savages of Cymiae"},
    {"dihmohn", "The Dih-Mohn Flotilla"},    {"edyn", "The Edyn Mandate"},           {"florzen", "The Florzen Profiteers"},
    {"freesystems", "The Free Systems Compact"}, {"ghemina", "The Ghemina Raiders"}, {"ghoti", "The Ghoti Wayfarers"},
    {"gledge", "The Gledge Union"},          {"ilyxum", "The Augurs of Ilyxum"},     {"kollecc", "The Kollecc Society"},
    {"kolume", "The Monks of Kolume"},       {"kortali", "The Kortali Tribunal"},    {"kyro", "The Kyro Sodality"},
    {"lanefir", "The Lanefir Remnants"},     {"lizho", "The Li-Zho Dynasty"},        {"ltokk", "The L'tokk Khrask"},
    {"mirveda", "The Mirveda Protectorate"}, {"mortheus", "The Glimmer of Mortheus"}, {"myko", "The Myko-Mentori"},
    {"nivyn", "The Nivyn Star Kings"},       {"nokar", "The Nokar Sellships"},       {"olradin", "The Olradin League"},
    {"rohdhna", "Roh'Dhna Mechatronics"},    {"tnelis", "The Tnelis Syndicate"},     {"vaden", "The Vaden Banking Clans"},
    {"vaylerian", "The Vaylerian Scourge"},  {"veldyr", "The Veldyr Sovereignty"},   {"rhodun", "The Zealots of Rhodun"},
    {"zelian", "The Zelian Purifier"}};
// Blue Reverie (0.83.0, TacticBlue): its factions in this game, playable one by one (owner 2026-11-23 20:58: only these five)
const SoonFaction kBlueReverie[] = {{"atokera", "The Atokera Legacy"}, {"belkosea", "The Belkosea Allied States"},
                                    {"pharadn", "The Pharad'n Order"}, {"qhet", "The Qhet Republic"},
                                    {"uydai", "The Uydai Conclave"}};

struct SeatSetup {
    bool human = false;
    int faction = -1;  // index into kFactions, -1 = random
    int tribunii = -1; // the Council Keleres: whose home they take (Mentak / Xxcha / Argent), -1 = random
    int color = -1;    // -1 = the next free colour
    int level = 3;     // computer difficulty (Competitive by default - owner)
    int arch = -1;     // computer personality, -1 = random
    std::string commander;  // (0.80.11) a rival commander you have beaten in the Career, sitting here ("" none)
};

class NewGameScene : public Scene {
public:
    NewGameScene() {
        std::vector<int> fs;
        for (int f = 0; f < kFactionCount; ++f)
            if (kFactions[f].source != Source::ThundersEdge && kFactions[f].source != Source::Discordant &&
                kFactions[f].source != Source::BlueReverie)
                fs.push_back(f);  // (Thunder's Edge, Discordant Stars, Blue Reverie: their own groups)
        std::sort(fs.begin(), fs.end(), [](int a, int b) { return std::strcmp(factionShortName(a), factionShortName(b)) < 0; });
        // the Council Keleres (0.21.0): random home, or the Mentak / Xxcha / Argent home
        const int keleres = findFaction("keleres");
        for (int f : fs) {
            factions_.push_back({f, -1});
            if (f == keleres)
                for (const char* k : {"mentak", "xxcha", "argent"}) factions_.push_back({f, findFaction(k)});
        }
    }
    static std::string choiceName(int f, int t) {
        if (f < 0) return "Random";
        if (f == findFaction("keleres")) return t < 0 ? "Keleres (random home)" : std::string("Keleres (") + factionShortName(t) + " home)";
        return factionShortName(f);
    }

    // 0.63.0 (owner's document): Mecatol Rex, Round 1 trade and AI speed moved into "Game options"; Hyperlanes into the Players
    // window; Galaxy opens its own window (draft / random / seed, fair or unbalanced, a preview)
    enum Row { kPlayers, kSeats, kVP, kGalaxy, kOptions, kStart, kRows };
    enum Col { kWho, kFaction, kColour, kLevel, kPers, kCols };

    int humans() const {
        int n = 0;
        for (int i = 0; i < players_; ++i)
            if (seats_[size_t(i)].human) ++n;
        return n;
    }
    // the seat you sit in: the one at the bottom of the map if a person plays it, else the first person's
    int layout() const { return hyperlanes_ ? 0 : 1; }  // (0.58.0) Setup::layout
    int primary() const {
        const int bottom = std::max(0, southernmostSeat(players_, layout()));
        if (seats_[size_t(bottom)].human) return bottom;
        for (int i = 0; i < players_; ++i)
            if (seats_[size_t(i)].human) return i;
        return -1;
    }
    void resetSeats() {
        for (SeatSetup& s : seats_) s = SeatSetup();
        const int bottom = std::max(0, southernmostSeat(players_, layout()));
        seats_[size_t(bottom)].human = true;
        seats_[size_t(bottom)].color = myColor_;
    }

    // ---------------------------------------------------------------- the faction menu
    bool factionOffered(int f) const {  // (in the faction list for the modules ticked)
        for (const auto& e : factions_)
            if (e.first == f) return true;
        return false;
    }
    bool factionFree(int f, int except, int tribunii = -1) const {
        for (int i = 0; i < players_; ++i) {
            if (i == except) continue;
            const SeatSetup& o = seats_[size_t(i)];
            if (o.faction == f) return false;
            if (tribunii >= 0 && o.faction == tribunii) return false;  // that home is in use
            if (o.tribunii >= 0 && o.tribunii == f) return false;      // the Keleres took this home
        }
        return true;
    }
    bool colourFree(int c, int except) const {
        if (!profile::colourUnlocked(c)) return false;  // 0.80.0: a league's colour, not earned yet
        for (int i = 0; i < players_; ++i)
            if (i != except && seats_[size_t(i)].color == c) return false;
        return true;
    }
    void change(int step) {
        SeatSetup& s = seats_[size_t(seatSel_)];
        switch (Col(colSel_)) {
            case kWho: s.human = !s.human; break;
            case kFaction: {  // Random, then the factions in name order, skipping ones other seats took
                const int count = int(factions_.size());
                const int n = count + 1;
                int pos = 0;
                for (int k = 0; k < count; ++k)
                    if (factions_[size_t(k)].first == s.faction && factions_[size_t(k)].second == s.tribunii) pos = k + 1;
                for (int tries = 0; tries < n; ++tries) {
                    pos = ((pos + step) % n + n) % n;
                    const int f = pos == 0 ? -1 : factions_[size_t(pos - 1)].first;
                    const int t = pos == 0 ? -1 : factions_[size_t(pos - 1)].second;
                    if (f < 0 || factionFree(f, seatSel_, t)) {
                        s.faction = f;
                        s.tribunii = t;
                        break;
                    }
                    step = step > 0 ? 1 : -1;
                }
                break;
            }
            case kColour: {  // Auto, then the sixteen colours (0.62.0: the classic 8 + 8 new), skipping taken ones
                constexpr int kPos = pcolours::kCount + 1;
                int pos = s.color + 1;
                for (int tries = 0; tries < kPos; ++tries) {
                    pos = ((pos + (step > 0 ? 1 : -1)) % kPos + kPos) % kPos;
                    const int c = pos - 1;
                    if (c < 0 || colourFree(c, seatSel_)) {
                        s.color = c;
                        break;
                    }
                }
                break;
            }
            case kLevel:
                if (!s.human) s.level = ((s.level + (step > 0 ? 1 : -1)) % kLevelCount + kLevelCount) % kLevelCount;
                break;
            case kPers:
                if (!s.human && s.level < kAiLevelAI) {  // (the A.I. has no personality)
                    const int m = int(Arch::Count) + 1;  // Random + the archetypes
                    if (!s.commander.empty()) s.commander.clear(), s.arch = step > 0 ? -2 : int(Arch::Count);  // (back to the archetypes)
                    s.arch = ((s.arch + 1 + (step > 0 ? 1 : -1)) % m + m) % m - 1;
                }
                break;
            default: break;
        }
    }
    void updateMenu(const Input& in) {
        // 0.62.1 (owner's document: a Confirm button at the bottom - before, only B / Backspace / a right click left the table): the
        // button is a stop of its own after the last seat (and before the first); A, START or a click on it keeps the choices and goes
        // back to the New Game screen
        if (onConfirm_) {
            if (in.pressed(Btn::Up)) onConfirm_ = false, seatSel_ = players_ - 1;
            else if (in.pressed(Btn::Down)) onConfirm_ = false, seatSel_ = 0;
            else if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Start) || in.pressed(Btn::Menu)) menuOpen_ = onConfirm_ = false;
            return;
        }
        if (in.pressed(Btn::Up)) {
            if (seatSel_ == 0) onConfirm_ = true;
            else --seatSel_;
        }
        if (in.pressed(Btn::Down)) {
            if (seatSel_ >= players_ - 1) onConfirm_ = true;
            else ++seatSel_;
        }
        if (onConfirm_) return;
        if (in.pressed(Btn::Left)) colSel_ = (colSel_ + kCols - 1) % kCols;
        if (in.pressed(Btn::Right)) colSel_ = (colSel_ + 1) % kCols;
        if (in.pressed(Btn::A)) {
            if (colSel_ == kFaction) openPicker();  // 0.28.2 (owner): the faction picker
            else if (colSel_ == kColour || ((colSel_ == kLevel || colSel_ == kPers) && !seats_[size_t(seatSel_)].human)) {
                if (!(colSel_ == kPers && seats_[size_t(seatSel_)].level >= kAiLevelAI)) openOpt();
            }
            else change(1);
        }
        if (in.pressed(Btn::Y)) change(-1);
        if (colSel_ == kFaction && in.pressed(Btn::R1)) change(5);
        if (colSel_ == kFaction && in.pressed(Btn::L1)) change(-5);
        if (in.pressed(Btn::X)) {  // back to random / automatic
            SeatSetup& s = seats_[size_t(seatSel_)];
            if (colSel_ == kFaction) s.faction = s.tribunii = -1;
            else if (colSel_ == kColour) s.color = -1;
            else if (colSel_ == kPers) s.arch = -1, s.commander.clear();
            else if (colSel_ == kLevel) s.level = 3;
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Start) || in.pressed(Btn::Menu)) menuOpen_ = false;
    }

    void update(App& app, const Input& in, uint32_t now) override {
        teOn_ = (app.settings().getInt("modules", ti::kModCodex) & ti::kModThundersEdge) != 0;
        modules_ = app.settings().getInt("modules", ti::kModCodex);
        dsOn_ = (app.settings().getInt("modules", ti::kModCodex) & ti::kModDiscordant) != 0;
        // (0.85.3, owner: no Blue Reverie box - its factions are secret unlockables earned in the Career) on when any is unlocked
        brMask_ = profile::brUnlockedMask();
        brOn_ = brMask_ != 0;
        if (pending_) {  // the transition plays; then the game
            if (!startAt_) {
                startAt_ = now;
                transition::pickStyle();  // 0.58.4: blue or red Lazaxian letters, at random
                // 0.30.0 (owner): the title's song fades out with the flood; GAME START brings the game's
                audio::musicFadeOut(int(transition::kFlood + transition::kReveal / 2));
            }
            if (now - startAt_ >= transition::kFade + transition::kFlood) {
                transition::pending = true;
                std::unique_ptr<GameSession> s = std::move(pending_);
                app.pop();
                app.push(makeGameScene(std::move(s)));
            }
            return;
        }
        if (first_) {
            speed_ = app.settings().getInt("ai_speed", 1);
            players_ = std::max(3, std::min(8, app.settings().getInt("players", 6)));
            myColor_ = std::max(-1, std::min(pcolours::kCount - 1, app.settings().getInt("color", 0)));  // (0.62.0: up to the 16th)
            freeTrade_ = app.settings().getInt("trade_r1", 1) != 0;
            contested_ = app.settings().getInt("mecatol_contested", 1) != 0;  // (0.54.0)
            hyperlanes_ = app.settings().getInt("hyperlanes", 1) != 0;          // (0.58.0)
            draft_ = app.settings().getInt("galaxy_draft", 1) != 0;
            galaxy_ = std::max(0, std::min(2, app.settings().getInt("galaxy_mode", draft_ ? 1 : 0)));  // 0.43.0: + Seeded
            mapSeed_ = uint32_t(std::max(1, app.settings().getInt("map_seed", int(now % 99991u) + 1)));
            fair_ = app.settings().getInt("galaxy_fair", 1) != 0;  // (0.63.0)
            resetSeats();
            first_ = false;
        }
        if (pickerOpen_) {
            updatePicker(in);
            return;
        }
        if (optOpen_) {  // (0.61.4) the colour / difficulty / personality window
            updateOpt(in);
            return;
        }
        if (menuOpen_) {
            updateMenu(in);
            return;
        }
        if (pop_ != kNoPop) {  // (0.63.0) the Players, Galaxy and Game options windows
            updatePop(in, now);
            return;
        }
        if (in.pressed(Btn::Up)) sel_ = (sel_ + kRows - 1) % kRows;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % kRows;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        if (in.pressed(Btn::A)) {
            switch (sel_) {
                case kSeats:
                    menuOpen_ = true;
                    onConfirm_ = false;
                    seatSel_ = std::max(0, primary());
                    colSel_ = kFaction;
                    return;
                case kPlayers: openPop(kPopPlayers); return;
                case kGalaxy: openPop(kPopGalaxy); return;
                case kOptions: openPop(kPopOptions); return;
                case kVP: vp_ = nextVp(vp_, 1); break;  // (0.82.6, owner: 12 points as in the leagues)
                default: break;
            }
        }
        const int step = in.pressed(Btn::Right) ? 1 : in.pressed(Btn::Left) ? -1 : 0;
        if (step) {
            switch (sel_) {
                case kPlayers: setPlayers(players_ + step); break;
                case kVP: vp_ = nextVp(vp_, step); break;
                case kGalaxy: galaxy_ = (galaxy_ + (step > 0 ? 1 : 2)) % 3; break;  // Random, Draft, Seeded (0.43.0)
                default: break;
            }
        }
        if ((in.pressed(Btn::A) && sel_ == kStart) || in.pressed(Btn::Start)) start(app, now);
    }

    void setPlayers(int n) {
        players_ = std::max(3, std::min(8, n));
        resetSeats();
    }
    void setHyperlanes(bool on) {  // (0.58.0) the seat at the bottom of the map can change: your seat moves with it
        if (on == hyperlanes_) return;
        const int was = std::max(0, southernmostSeat(players_, layout()));
        hyperlanes_ = on;
        const int now2 = std::max(0, southernmostSeat(players_, layout()));
        if (was != now2) std::swap(seats_[size_t(was)], seats_[size_t(now2)]);
    }

    void start(App& app, uint32_t now) {
        const int me = primary();
        const bool watch = me < 0;  // nobody at the device: watch the computer play every seat
        app.settings().setInt("ai_speed", speed_);
        app.settings().setInt("players", players_);
        if (me >= 0) app.settings().setInt("color", seats_[size_t(me)].color);
        app.settings().setInt("trade_r1", freeTrade_ ? 1 : 0);
        app.settings().setInt("mecatol_contested", contested_ ? 1 : 0);
        app.settings().setInt("hyperlanes", hyperlanes_ ? 1 : 0);
        app.settings().setInt("galaxy_draft", galaxy_ == 1 ? 1 : 0);
        app.settings().setInt("galaxy_mode", galaxy_);
        app.settings().setInt("map_seed", int(mapSeed_));
        app.settings().setInt("galaxy_fair", fair_ ? 1 : 0);
        app.settings().save();
        Setup s;
        s.players = players_;
        s.human = watch ? std::max(0, southernmostSeat(players_, layout())) : me;
        s.vpGoal = vp_;
        s.humanColor = -1;
        s.trade = 1 | (freeTrade_ ? 2 : 0) | 4 | 8;  // community X-1 trades; round-1 free refills when a bot plays Trade (house rule)
        s.draft = galaxy_ == 1 ? 1 : 0;            // players build the galaxy (0.15.0) or a random one
        s.mapSeed = galaxy_ == 2 ? mapSeed_ : 0;   // ...or the map of the player's number (0.43.0)
        s.unbalanced = galaxy_ != 2 && !fair_ ? 1 : 0;  // (0.63.0) "Fair slices" unticked: the first deal as it falls
        s.layout = layout();                        // the printed board without hyperlanes (0.58.0)
        s.modules = app.settings().getInt("modules", ti::kModCodex);  // the Modules screen (0.31.0)
        s.modules = (s.modules & ~ti::kModBlueReverie) | (brOn_ ? ti::kModBlueReverie : 0);  // (0.85.3: by the unlocks, not a box)
        s.brMask = brMask_;
        if (s.modules & ti::kModThundersEdge) {  // galactic events (0.36.0): only the playable ones
            const int ev = app.settings().getInt("events", 0);
            for (int k = 0; k < ti::kEventCount; ++k)
                if (((ev >> k) & 1) && ti::galacticEvent(k).done) s.events |= 1 << k;
        }
        s.extras = 1 | 2 | 4 | 8 | 16 | 32 | 64 | (contested_ ? 128 : 0);  // Kingslay (0.19.0); the Keleres can be dealt (0.21.0);
                                                  // no random Rusher / Chaotic for Competitive bots (0.27.0);
                                                  // AIDA / X-1 in the agenda (0.30.0); Trade Convoys (0.32.0) and
                                                  // Dark Pact (0.32.1) as actions; Mecatol Rex contested (0.54.0)
        s.autoplay = watch;
        if (!watch) s.habits = habits::load(app.settings());  // (0.54.0) the bots know your habits from earlier games
        s.seed = now * 2654435761u ^ 0x7154u;
        s.factions.assign(players_, -1);
        s.colors.assign(players_, -1);
        s.levels.assign(players_, -1);
        s.archetypes.assign(players_, -1);
        s.tribunii.assign(players_, -1);
        int levelCount[kLevelCount] = {};
        for (int i = 0; i < players_; ++i) {
            const SeatSetup& st = seats_[size_t(i)];
            // (0.85.3: a Blue Reverie faction not unlocked - picked in an older version - is Random)
            s.factions[size_t(i)] = st.faction >= 0 && kFactions[st.faction].source == Source::BlueReverie && !brInPool(st.faction, kBrLevel, brMask_)
                                        ? -1 : st.faction;
            s.tribunii[size_t(i)] = st.tribunii;
            s.colors[size_t(i)] = st.color;
            if (st.human && i != s.human) s.humans |= 1 << i;
            if (!st.human || watch) {
                s.levels[size_t(i)] = st.level;
                s.archetypes[size_t(i)] = st.arch;
                ++levelCount[st.level];
            }
        }
        s.aiLevel = int(std::max_element(levelCount, levelCount + kLevelCount) - levelCount);  // the most common one
        // (0.80.11) rival commanders you have beaten: their personality, their name, their grudge from your Career games - and their
        // own faction when the seat's is Random and it's free
        for (int i = 0; i < players_; ++i) {
            const SeatSetup& st = seats_[size_t(i)];
            const league::RivalDef* r = st.commander.empty() ? nullptr : league::rival(st.commander);
            if (!r || st.human || st.level >= kAiLevelAI) continue;
            if (s.commanders.empty()) s.commanders.assign(players_, std::string()), s.grudges.assign(players_, 0);
            s.commanders[size_t(i)] = r->id;
            s.archetypes[size_t(i)] = r->arch;
            s.grudges[size_t(i)] = leaguerun::grudgeOf(r->id);
            const int sig = r->faction && *r->faction ? findFaction(r->faction) : -1;
            if (st.faction < 0 && sig >= 0 && factionOffered(sig) && factionFree(sig, i)) {
                bool taken = false;
                for (int j = 0; j < players_; ++j) taken |= j != i && s.factions[size_t(j)] == sig;
                if (!taken) s.factions[size_t(i)] = sig;
            }
        }
        std::unique_ptr<GameSession> session(new GameSession());
        if (!session->startNew(s)) {
            LOG("newgame: engine failed to start: %s", session->engine().crashMessage().c_str());
            error_ = "The game could not start: " + session->engine().crashMessage();
            return;
        }
        LOG("newgame: %d players, %d at the device%s", players_, humans(), watch ? ", watching" : "");
        // 0.28.0 (owner): the game-start transition (not in the scripted tests)
        if (app.platform().headless() && !std::getenv("TI4_INTRO")) {
            app.pop();
            app.push(makeGameScene(std::move(session)));
            return;
        }
        pending_ = std::move(session);
        startAt_ = 0;  // (0.28.1: the music plays on)
    }
    std::unique_ptr<GameSession> pending_;
    uint32_t startAt_ = 0;

    std::string seatsSummary() const {
        const int h = humans();
        int levels[kLevelCount] = {};
        for (int i = 0; i < players_; ++i)
            if (!seats_[size_t(i)].human) ++levels[seats_[size_t(i)].level];
        std::string lv;
        int kinds = 0;
        for (int l = 0; l < kLevelCount; ++l)
            if (levels[l]) {
                ++kinds;
                lv = kLevels[l];
            }
        if (kinds > 1) lv = "mixed";
        std::string who = h == 0 ? "computer only (watch)" : h == 1 ? "you + " + std::to_string(players_ - 1) + " computer"
                                                                    : std::to_string(h) + " people (hot-seat) + " +
                                                                          std::to_string(players_ - h) + " computer";
        return who + (players_ - h > 0 ? " - " + lv : std::string());
    }

    void draw(App& app, Canvas& c, uint32_t now) override {
        drawScreen(app, c, now);
        if (pending_) transition::drawCover(c, startAt_ ? now - startAt_ : 0);
    }
    // 0.62.1: on the seat table and its pickers a click on nothing (the panel's background, a gap) does nothing - it used to act as A
    // on the highlighted cell (a click just under the Confirm button opened the faction picker); cells, rows and buttons still take it
    bool bareClickIsA(float x, float y) const override {
        (void)x, (void)y;
        return !(menuOpen_ || pickerOpen_ || optOpen_);
    }
    bool animating() const override {
        if (pending_ != nullptr || retro::on() || retro::hd() || optOpen_ || (menuOpen_ && onConfirm_)) return true;
        if (menuOpen_)  // (0.62.0) a seat in one of the new colours: its swatch moves
            for (int i = 0; i < players_; ++i)
                if (seats_[size_t(i)].color >= 0 && pcolours::animated(seats_[size_t(i)].color)) return true;
        return false;
    }
    // 0.44.1 (owner: the wheel "snapping to the factions instead of allowing me to free-scroll"): in the picker the
    // wheel scrolls the grid itself (updatePicker), not the chosen faction
    bool wheelScrolls(float x, float y) const override { return pickerOpen_ ? false : Scene::wheelScrolls(x, y); }  // the backdrops move (0.30.0, 0.33.1)
    void drawScreen(App&, Canvas& c, uint32_t now) {
        (void)now;
        if (retro::on()) {  // 0.30.0 (owner): the Retrowave New Game backdrop
#if defined(__arm__)
            backdrops::drawRetroNewGame(c, now);  // (the Miyoo keeps 0.60.0's chunky one)
#else
            newgamebg::draw(c, now);  // 0.60.1 (owner): in the title's detail
#endif
        }
        else if (!(retro::hd() && backdrops::drawVoidfallNewGame(c, now)))  // 0.33.1 (owner): Voidfall's own
            drawSpaceBackground(c, true);
        if (retro::hd()) {  // (0.33.1) the title and the line under the panel kept readable over the painting
            c.fillRoundRect(240, 18, 160, 30, 10, rgb(10, 4, 18), 170);
            c.fillRoundRect(40, 368, 560, 20, 8, rgb(10, 4, 18), 175);
        }
        drawTextAligned(c, FONT_LG, 0, 22, 640, Align::Center, "New game", theme::gold);
        Rect r{100, 58, 440, 284};  // (0.63.0: five rows - the rest went into the windows)
        drawPanel(c, r, theme::panel, theme::border, 12, retro::on() || retro::hd() ? 228 : 255);
        struct R {
            const char* label;
            std::string value, detail;
            bool arrows;
        } rows[kRows - 1] = {
            {"Players", std::to_string(players_), players_ == 6 ? "Choose the count and see the map" : hyperlanes_ ? "Hyperlanes on" : "No hyperlanes: the printed board", true},
            {"Factions & seats", "Set up", seatsSummary(), false},
            {"Victory points", std::to_string(vp_), "", true},
            {"Galaxy", galaxyName(), galaxyDetail(), true},
            {"Game options", "Set up", optionsSummary(), false},
        };
        const int rowH = 40;
        int y = r.y + 16;
        const bool covered = menuOpen_ || pop_ != kNoPop;
        for (int i = 0; i < kRows - 1; ++i) {
            const bool sel = i == sel_;
            if (!covered) {  // the mouse (0.27.0): point at a row, click it to open its window (or change it)
                hits::add(Rect{r.x + 12, y - 4, r.w - 24, rowH - 4}, [this, i]() { sel_ = i; });
                if (sel && rows[i].arrows) {
                    hits::addButton(Rect{r.x + r.w - 228, y - 4, 26, rowH - 4}, Btn::Left);
                    hits::addButton(Rect{r.x + r.w - 46, y - 4, 30, rowH - 4}, Btn::Right);
                }
            }
            if (sel) c.fillRoundRect(r.x + 12, y - 4, r.w - 24, rowH - 4, 6, theme::panelHi);
            drawText(c, FONT_MDB, r.x + 26, y + (rows[i].detail.empty() ? 7 : 2), rows[i].label, sel ? theme::text : theme::dim);
            const int right = r.x + r.w - 46;
            drawTextAligned(c, FONT_MDB, r.x, y + (rows[i].detail.empty() ? 7 : 2), right - r.x, Align::Right, rows[i].value, theme::gold);
            if (!rows[i].detail.empty())
                drawTextAligned(c, FONT_XS, r.x, y + 21, right - r.x, Align::Right, fitText(FONT_XS, rows[i].detail, 300), theme::faint);
            if (sel && rows[i].arrows) {
                drawText(c, FONT_SM, r.x + r.w - 220, y + 9, "\xE2\x97\x80", theme::dim);
                drawText(c, FONT_SM, r.x + r.w - 38, y + 9, "\xE2\x96\xB6", theme::dim);
            }
            y += rowH;
        }
        const bool sel = sel_ == kStart;
        const bool watch = primary() < 0;
        Rect b{r.x + (r.w - 180) / 2, y + 10, 180, 40};
        if (!covered) hits::add(b, [this]() { sel_ = kStart; });
        c.fillRoundRect(b.x, b.y, b.w, b.h, 10, sel ? theme::gold : theme::panelHi);
        drawTextAligned(c, FONT_LG, b.x, b.y + 8, b.w, Align::Center, watch ? "Watch game" : "Start game", sel ? theme::black : theme::text);
        drawTextAligned(c, FONT_XS, 0, 372, 640, Align::Center,
                        keyLabel("A") + " on a row opens its window: the player count and its map, the seats, the galaxy, game options.",
                        theme::faint);
        if (!error_.empty()) drawTextAligned(c, FONT_SM, 0, 390, 640, Align::Center, error_, theme::red);
        if (pop_ != kNoPop) {
            drawPop(c, now);
            return;
        }
        if (menuOpen_) {
            drawMenu(c);
            if (pickerOpen_) {
                hits::clear();  // 0.28.4: the seat table under the picker must not take the mouse (it changed another seat)
                drawPicker(c, now);
            }
            if (optOpen_) drawOpt(c, now);  // (0.61.4)
            return;
        }
        drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", "Open"}, {"START", watch ? "Watch" : "Begin"}, {"B", "Back"}});
    }

    std::string galaxyName() const { return galaxy_ == 1 ? "Draft" : galaxy_ == 2 ? "Seed " + std::to_string(mapSeed_) : "Random"; }
    std::string galaxyDetail() const {
        if (galaxy_ == 2) return "The same number always makes the same map";
        return std::string(galaxy_ == 1 ? "Everyone places tiles in turn" : "A random map") + (fair_ ? " - fair slices" : " - unbalanced");
    }
    std::string optionsSummary() const {
        return std::string(contested_ ? "Mecatol contested" : "Mecatol calm") + " \xC2\xB7 " + (freeTrade_ ? "free refills" : "Trade as printed") +
               " \xC2\xB7 AI " + kSpeedNames[speed_];
    }

    // 0.58.0 (owner): Hyperlanes On = the usual maps; Off = the printed board without them, at any player count
    std::string hyperlaneDetail() const {
        if (players_ == 6) return "Six players never use them";
        if (hyperlanes_) return "The usual map: hyperlanes fill the gaps";
        switch (players_) {
            case 3: return "The printed 3-player board, 8 tiles each";
            case 4: return "The printed 4-player board, 8 tiles each";
            case 5: return "Printed board; 3 seats get +2/+4/+2 trade goods";
            case 7: return "The 7-player board, its gap filled with systems";
            default: return "The printed 8-player board, 61 systems";
        }
    }

    // ============================================================ 0.63.0: the New Game windows (owner's document)
    // "Players": the count and an example map for it, Hyperlanes as a tick box that redraws the map. "Galaxy": Draft, Random or
    // a seed (typed on the keyboard, or stepped), "Fair slices" for the maps that aren't seeded, and the map it makes - the
    // seed's own, or an example - with what each seat's slice (or draft hand) holds. "Game options": Mecatol Rex, Round 1
    // trade, AI speed. D-pad / mouse to point, A or a click to change, the Done button (or B, or a click beside it) closes.
    enum Pop { kNoPop, kPopPlayers, kPopGalaxy, kPopOptions };
    int pop_ = kNoPop, popSel_ = 0;
    std::string seedTyped_;  // the digits typed into the seed so far (keyboard)
    static constexpr int kGalaxyButtons[3] = {1, 0, 2};  // the Galaxy window's buttons, left to right: Draft, Random, Seed

    void openPop(int which) {
        pop_ = which;
        popSel_ = 0;
        seedTyped_.clear();
    }
    int popRows() const { return pop_ == kPopPlayers ? 3 : 4; }  // (the last row is the Done button)
    void setSeed(uint32_t v) { mapSeed_ = std::max<uint32_t>(1, std::min<uint32_t>(999999u, v)); }

    void updatePop(const Input& in, uint32_t now) {
        const int n = popRows();
        if (pop_ == kPopGalaxy && !in.typed().empty()) {  // digits typed: the seed (a seeded map from then on)
            if (galaxy_ != 2) galaxy_ = 2;
            if (popSel_ != 1) seedTyped_.clear();
            popSel_ = 1;
            for (char ch : in.typed())
                if (seedTyped_.size() < 6 && !(seedTyped_.empty() && ch == '0')) seedTyped_ += ch;
            if (!seedTyped_.empty()) setSeed(uint32_t(std::atoi(seedTyped_.c_str())));
            return;
        }
        if (pop_ == kPopGalaxy && popSel_ == 1 && !seedTyped_.empty() && in.pressed(Btn::B)) {  // Backspace: the last digit
            seedTyped_.pop_back();
            if (!seedTyped_.empty()) setSeed(uint32_t(std::atoi(seedTyped_.c_str())));
            return;
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu) || in.pressed(Btn::Start) || (popSel_ == n - 1 && in.pressed(Btn::A))) {
            pop_ = kNoPop;
            return;
        }
        auto usable = [&](int row) { return !(pop_ == kPopGalaxy && ((row == 1 && galaxy_ != 2) || (row == 2 && galaxy_ == 2))); };
        if (in.pressed(Btn::Up) || in.pressed(Btn::Down)) {
            const int d = in.pressed(Btn::Up) ? -1 : 1;
            for (int k = 0; k < n; ++k) {
                popSel_ = (popSel_ + d + n) % n;
                if (usable(popSel_)) break;
            }
            seedTyped_.clear();
        }
        const int step = in.pressed(Btn::Right) ? 1 : in.pressed(Btn::Left) ? -1 : 0;
        const bool a = in.pressed(Btn::A);
        switch (pop_) {
            case kPopPlayers:
                if (popSel_ == 0 && (step || a)) setPlayers(players_ + (step ? step : 1) > 8 ? 3 : players_ + (step ? step : 1));
                if (popSel_ == 1 && (step || a)) setHyperlanes(!hyperlanes_);
                break;
            case kPopGalaxy:
                if (popSel_ == 0 && (step || a)) {
                    int k = 0;
                    while (k < 3 && kGalaxyButtons[k] != galaxy_) ++k;
                    galaxy_ = kGalaxyButtons[(k + (step < 0 ? 2 : 1)) % 3];
                }
                if (popSel_ == 1 && galaxy_ == 2) {
                    if (step || in.pressed(Btn::L1) || in.pressed(Btn::R1) || in.pressed(Btn::X)) seedTyped_.clear();
                    if (step > 0) setSeed(mapSeed_ + 1);
                    if (step < 0) setSeed(mapSeed_ > 1 ? mapSeed_ - 1 : 1);
                    if (in.pressed(Btn::R1)) setSeed(mapSeed_ + 100);
                    if (in.pressed(Btn::L1)) setSeed(mapSeed_ > 100 ? mapSeed_ - 100 : 1);
                    if (in.pressed(Btn::X) || a) setSeed(1 + (now * 2654435761u >> 8) % 99991u);
                }
                if (popSel_ == 2 && (step || a) && galaxy_ != 2) fair_ = !fair_;
                break;
            case kPopOptions:
                if (popSel_ == 0 && (step || a)) contested_ = !contested_;
                if (popSel_ == 1 && (step || a)) freeTrade_ = !freeTrade_;
                if (popSel_ == 2 && (step || a)) speed_ = (speed_ + (step < 0 ? 3 : 1)) % 4;
                break;
            default: break;
        }
        if (!usable(popSel_)) popSel_ = n - 1;  // (the row it was on went grey: Done)
    }

    // ---- the windows' pieces
    void popFrame(Canvas& c, Rect r, const std::string& title) {
        hits::clear();  // (the screen under the window doesn't take the mouse)
        hits::addBackdrop(Rect{0, 0, 640, 446}, [this] { pop_ = kNoPop; });  // (a click beside it closes it)
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 150);
        c.fillRect(r.x + 5, r.y + 6, r.w, r.h, rgb(0, 0, 0), 110);
        drawPanel(c, r, rgb(12, 16, 30), theme::gold, 12, 250);
        hits::addBlock(r);
        drawTextAligned(c, FONT_LG, r.x, r.y + 8, r.w, Align::Center, title, theme::gold);
    }
    void popRowFrame(Canvas& c, Rect rr, int idx, bool enabled) {
        if (enabled) hits::add(rr, [this, idx] {
                if (popSel_ != idx) seedTyped_.clear();
                popSel_ = idx;
            });
        if (popSel_ == idx) c.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 6, theme::panelHi);
    }
    // a row with a value that steps (arrows either side of it while it is chosen)
    void popValueRow(Canvas& c, Rect rr, int idx, const std::string& label, const std::string& value, const std::string& detail,
                     bool enabled = true) {
        popRowFrame(c, rr, idx, enabled);
        const bool sel = popSel_ == idx && enabled;
        const int ty = detail.empty() ? rr.y + (rr.h - FONT_MDB.lineHeight) / 2 : rr.y + 3;
        drawText(c, FONT_MDB, rr.x + 12, ty, label, !enabled ? theme::faint : sel ? theme::text : theme::dim);
        const int right = rr.x + rr.w - 30, vw = textWidth(FONT_MDB, value);
        drawTextAligned(c, FONT_MDB, rr.x, ty, right - rr.x, Align::Right, value, enabled ? theme::gold : theme::faint);
        if (!detail.empty())
            drawTextAligned(c, FONT_XS, rr.x + 12, rr.y + rr.h - 15, right - rr.x - 12, Align::Right, fitText(FONT_XS, detail, rr.w - 50),
                            theme::faint);
        if (sel) {
            drawText(c, FONT_SM, right - vw - 20, ty + 2, "\xE2\x97\x80", theme::dim);
            drawText(c, FONT_SM, right + 8, ty + 2, "\xE2\x96\xB6", theme::dim);
            hits::addButton(Rect{right - vw - 26, rr.y, 22, rr.h}, Btn::Left);
            hits::addButton(Rect{right + 2, rr.y, 26, rr.h}, Btn::Right);
        }
    }
    // a row with a tick box
    void popTickRow(Canvas& c, Rect rr, int idx, const std::string& label, bool on, const std::string& detail, bool enabled = true) {
        popRowFrame(c, rr, idx, enabled);
        const bool sel = popSel_ == idx && enabled;
        const int bx = rr.x + 12, by = rr.y + (detail.empty() ? (rr.h - 16) / 2 : 5);
        c.fillRoundRect(bx, by, 16, 16, 3, rgb(8, 10, 18));
        c.frameRoundRect(bx, by, 16, 16, 3, !enabled ? theme::border : sel ? theme::gold : theme::dim, sel ? 2 : 1);
        if (on) {
            const uint32_t tc = enabled ? theme::green : theme::faint;
            c.line(float(bx + 3), float(by + 8), float(bx + 7), float(by + 12), tc, 2.2f);
            c.line(float(bx + 7), float(by + 12), float(bx + 13), float(by + 4), tc, 2.2f);
        }
        drawText(c, FONT_MDB, bx + 24, by - 1, label, !enabled ? theme::faint : sel ? theme::text : theme::dim);
        if (!detail.empty()) drawText(c, FONT_XS, bx + 24, by + 18, fitText(FONT_XS, detail, rr.x + rr.w - bx - 30), theme::faint);
    }
    void popDone(Canvas& c, Rect b, int idx) {
        hits::add(b, [this, idx] { popSel_ = idx; });
        const bool sel = popSel_ == idx;
        c.fillRoundRect(b.x, b.y, b.w, b.h, 7, sel ? rgb(40, 76, 44) : rgb(22, 40, 28), 245);
        c.frameRoundRect(b.x, b.y, b.w, b.h, 7, sel ? theme::gold : theme::green, sel ? 2 : 1);
        drawTextAligned(c, FONT_MDB, b.x, b.y + (b.h - FONT_MDB.lineHeight) / 2, b.w, Align::Center, "Done", sel ? theme::text : theme::green);
    }

    // ---- the map previews
    struct Preview {
        ti::Game g;
        uint64_t key = ~0ull;
        bool ok = false;
        std::vector<int> owner;
        std::vector<ti::SliceStats> stats;
        MapView map;
    };
    Preview pvPlayers_, pvGalaxy_;
    // the colours the seats will have - as the game gives them out: the picks, then the classic eight in seat order
    std::vector<int> seatColours() const {
        std::vector<int> col(size_t(players_), -1);
        std::vector<bool> taken(size_t(pcolours::kCount), false);
        for (int i = 0; i < players_; ++i) {
            const int c = seats_[size_t(i)].color;
            if (c >= 0 && c < pcolours::kCount && !taken[size_t(c)]) col[size_t(i)] = c, taken[size_t(c)] = true;
        }
        for (int i = 0, next = 0; i < players_; ++i) {
            if (col[size_t(i)] >= 0) continue;
            while (next < pcolours::kClassic && taken[size_t(next)]) ++next;
            col[size_t(i)] = next < pcolours::kClassic ? next : 0;
            if (next < pcolours::kClassic) taken[size_t(next)] = true;
        }
        return col;
    }
    // mode 0: a random map (fair or not) from `number`; 1: the board before the draft, with the hands dealt; 2: the seed's
    // map. Built the way the game builds it, again only when something it depends on changes.
    void buildMap(Preview& pv, int mode, uint32_t number, bool fair) {
        const std::vector<int> cols = seatColours();
        uint64_t key = 1469598103934665603ull;
        auto mixIn = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mixIn(uint64_t(mode)), mixIn(number), mixIn(fair ? 1 : 0), mixIn(uint64_t(players_)), mixIn(uint64_t(modules_)), mixIn(uint64_t(layout()));
        for (int i = 0; i < players_; ++i) mixIn(uint64_t(seats_[size_t(i)].faction + 2) * 64u + uint64_t(cols[size_t(i)]));
        if (key == pv.key) return;
        pv.key = key;
        pv.g = ti::Game();
        pv.g.numPlayers = players_;
        pv.g.modules = uint8_t(modules_);
        pv.g.layout = uint8_t(layout());
        pv.g.players.assign(size_t(players_), ti::Player());
        std::vector<int> homes(size_t(players_), 0);
        for (int i = 0; i < players_; ++i) {
            const int f = seats_[size_t(i)].faction >= 0 ? seats_[size_t(i)].faction : i % ti::kFactionCount;  // (random seats: a stand-in home)
            homes[size_t(i)] = f;
            pv.g.players[size_t(i)].faction = int8_t(f);
            pv.g.players[size_t(i)].color = int8_t(cols[size_t(i)]);
        }
        ti::Rng mr(uint32_t(number * 2654435761u + uint32_t(players_)));  // (a seed: exactly as the game seeds it, 32-bit)
        std::string err;
        pv.owner.clear();
        pv.stats.clear();
        if (mode == 1) {
            pv.ok = ti::startGalaxyDraft(pv.g, mr, homes, &err, !fair);
            if (pv.ok) {  // what each hand holds
                pv.stats.assign(size_t(players_), ti::SliceStats());
                for (int p = 0; p < players_ && p < int(pv.g.draft.hands.size()); ++p)
                    for (int d : pv.g.draft.hands[size_t(p)]) {
                        ti::SliceStats& st = pv.stats[size_t(p)];
                        const ti::SystemDef& sd = ti::kSystems[d];
                        st.wormholes += sd.wormholes ? 1 : 0;
                        st.anomalies += sd.anomalies ? 1 : 0;
                        for (int k = 0; k < sd.planetCount; ++k) {
                            const ti::PlanetDef& pd = ti::kPlanets[sd.planets[k]];
                            st.res += pd.res, st.inf += pd.inf, st.optimal += std::max(pd.res, pd.inf);
                            st.planets += 1, st.legendary += pd.legendary ? 1 : 0, st.skips += pd.skip != ti::TechColor::None ? 1 : 0;
                        }
                    }
            }
        } else {
            pv.ok = ti::buildGalaxy(pv.g, mr, homes, &err, mode == 2, &pv.owner, mode == 0 && !fair);
            if (pv.ok) pv.stats = ti::sliceStats(pv.g, pv.owner, players_);
        }
        if (pv.ok) ti::computeAdjacency(pv.g);
    }
    void drawMap(Canvas& c, Preview& pv, Rect area, uint32_t now) {
        c.fillRoundRect(area.x, area.y, area.w, area.h, 8, rgb(4, 6, 12), 235);
        if (!pv.ok) {
            drawTextAligned(c, FONT_MD, area.x, area.y + area.h / 2 - 10, area.w, Align::Center, "No legal map - try another number.", theme::red);
            return;
        }
        pv.map.setInfoTiles(false);
        pv.map.fitTo(pv.g, area);
        pv.map.draw(c, pv.g, now);
    }
    // each seat's slice (or draft hand): resources, influence, the best of each planet, planets, legendary, wormholes, anomalies
    void drawSliceTable(Canvas& c, const Preview& pv, int x, int y) {
        static const char* kHeads[] = {"Res", "Inf", "Best", "Pl", "Leg", "Wh", "An"};
        constexpr int kColW = 30, kNameW = 92;
        for (int k = 0; k < 7; ++k) drawTextAligned(c, FONT_XS, x + kNameW + k * kColW, y, kColW, Align::Center, kHeads[k], theme::dim);
        y += 14;
        int lo = 1 << 20, hi = -1;
        for (int i = 0; i < players_ && i < int(pv.stats.size()); ++i) {
            const ti::SliceStats& st = pv.stats[size_t(i)];
            const uint32_t col = pcolours::base(pv.g.players[size_t(i)].color);
            c.fillRect(x, y + 2, 3, 11, col);
            const SeatSetup& ss = seats_[size_t(i)];
            const std::string who = "Seat " + std::to_string(i + 1) + (ss.faction >= 0 ? std::string(" ") + ti::factionShortName(ss.faction) : "");
            drawText(c, FONT_XS, x + 7, y, fitText(FONT_XS, who, kNameW - 10), pcolours::readable(col));
            const int v[7] = {st.res, st.inf, st.optimal, st.planets, st.legendary, st.wormholes, st.anomalies};
            for (int k = 0; k < 7; ++k)
                drawTextAligned(c, FONT_XS, x + kNameW + k * kColW, y, kColW, Align::Center, std::to_string(v[k]), k == 2 ? theme::gold : theme::text);
            lo = std::min(lo, st.optimal), hi = std::max(hi, st.optimal);
            y += 14;
        }
        if (hi >= 0)
            drawText(c, FONT_XS, x, y + 3, "Richest and poorest differ by " + std::to_string(hi - lo) + " (Best).", hi - lo >= 6 ? rgb(255, 200, 120) : theme::faint);
    }

    void drawPop(Canvas& c, uint32_t now) {
        if (pop_ == kPopPlayers) {
            const Rect r{40, 12, 560, 432};
            popFrame(c, r, "Players");
            popValueRow(c, Rect{56, 46, 528, 32}, 0, "Players", std::to_string(players_), "");
            popTickRow(c, Rect{56, 82, 528, 40}, 1, "Hyperlanes", hyperlanes_,
                       players_ == 6 ? "Six players never use them - the same map either way." : hyperlaneDetail());
            buildMap(pvPlayers_, 0, 2026u, true);
            drawMap(c, pvPlayers_, Rect{56, 128, 528, 252}, now);
            const int me = primary();
            const std::string you = me >= 0 ? "You play the seat at the bottom, in " + std::string(pcolours::info(seatColours()[size_t(me)]).name) + "."
                                             : "Nobody plays at the device: you watch.";
            drawTextAligned(c, FONT_XS, 56, 384, 528, Align::Center, "An example map for " + std::to_string(players_) + " players. " + you, theme::faint);
            popDone(c, Rect{250, 404, 140, 30}, 2);
            c.fillRect(0, 446, 640, 34, rgb(5, 8, 16));
            drawHintBar(c, {{"D-PAD", "Move"}, {"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", popSel_ == 2 ? "Done" : "Change"}, {"B", "Done"}});
        } else if (pop_ == kPopGalaxy) {
            const Rect r{8, 8, 624, 436};
            popFrame(c, r, "Galaxy");
            // the three ways to make the map
            popRowFrame(c, Rect{18, 44, 250, 36}, 0, true);
            static const char* kNames[3] = {"Draft", "Random", "Seed"};
            for (int k = 0; k < 3; ++k) {
                const Rect b{24 + k * 80, 48, 76, 28};
                const bool on = galaxy_ == kGalaxyButtons[k];
                c.fillRoundRect(b.x, b.y, b.w, b.h, 6, on ? theme::gold : rgb(22, 28, 46));
                c.frameRoundRect(b.x, b.y, b.w, b.h, 6, on ? theme::gold : theme::border, 1);
                drawTextAligned(c, FONT_SMB, b.x, b.y + 6, b.w, Align::Center, kNames[k], on ? theme::black : theme::text);
                const int mode = kGalaxyButtons[k];
                hits::addAction(b, [this, mode] {
                    galaxy_ = mode;
                    popSel_ = 0;
                    seedTyped_.clear();
                });
            }
            const bool seeded = galaxy_ == 2;
            popValueRow(c, Rect{18, 84, 250, 44}, 1, "Seed", seedTyped_.empty() || popSel_ != 1 ? std::to_string(mapSeed_) : seedTyped_ + "_",
                        seeded ? keyboardKeys() ? "Type it, or " + keyLabel("L1") + "/" + keyLabel("R1") + " by 100, " + keyLabel("X") + " new"
                                                : keyLabel("L1") + "/" + keyLabel("R1") + " by 100, " + keyLabel("X") + " a new one"
                               : "Only for a seeded map",
                        seeded);
            popTickRow(c, Rect{18, 132, 250, 44}, 2, "Fair slices", seeded || fair_,
                       seeded ? "A seeded map is always balanced" : fair_ ? "The most even of many deals" : "The first deal as it falls", !seeded);
            const char* about = galaxy_ == 1
                                    ? "Draft: everyone holds 3 blue and 2 red tiles and places them in turn, the speaker first, as in the rulebook. "
                                      "The map on the right is the board before anyone places a tile; the table is an example of the hands."
                                : galaxy_ == 0 ? "Random: the map is dealt when the game starts. The map on the right is an example - every game deals its own."
                                               : "Seed: the number makes the map. The same number and player count always make this same map - "
                                                 "share it with a friend to play the same galaxy.";
            int ay = 188;
            for (const std::string& l : wrapText(FONT_XS, about, 238)) {
                drawText(c, FONT_XS, 24, ay, l, theme::dim);
                ay += 13;
            }
            if (!seeded)
                for (const std::string& l : wrapText(FONT_XS, fair_ ? "Fair slices: of many deals the most even one is used - every seat gets about as much."
                                                                     : "Unbalanced: nothing is evened out - some seats start richer than others.",
                                                       238)) {
                    drawText(c, FONT_XS, 24, ay + 6, l, fair_ ? theme::faint : rgb(255, 200, 120));
                    ay += 13;
                }
            popDone(c, Rect{78, 404, 140, 30}, 3);
            // the map it makes, and its slices
            buildMap(pvGalaxy_, galaxy_ == 2 ? 2 : galaxy_ == 1 ? 1 : 0, galaxy_ == 2 ? mapSeed_ : 7u, galaxy_ == 2 || fair_);
            drawMap(c, pvGalaxy_, Rect{278, 44, 344, 226}, now);
            drawTextAligned(c, FONT_XS, 278, 273, 344, Align::Center,
                            galaxy_ == 2 ? "Seed " + std::to_string(mapSeed_) + ": the map you will play" : galaxy_ == 1 ? "The board before the draft (an example)" : "An example map",
                            theme::faint);
            if (pvGalaxy_.ok) drawSliceTable(c, pvGalaxy_, 282, 288);
            c.fillRect(0, 446, 640, 34, rgb(5, 8, 16));
            if (popSel_ == 1) drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Seed -/+1"}, {"L1/R1", "-/+100"}, {"X", "New seed"}, {"B", seedTyped_.empty() ? "Done" : "Delete"}});
            else drawHintBar(c, {{"D-PAD", "Move"}, {"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", popSel_ == 3 ? "Done" : "Change"}, {"B", "Done"}});
        } else if (pop_ == kPopOptions) {
            const Rect r{100, 92, 440, 252};
            popFrame(c, r, "Game options");
            popValueRow(c, Rect{116, 130, 408, 44}, 0, "Mecatol Rex", contested_ ? "Contested" : "Calm",
                        contested_ ? "Bots fight its holder for it - Imperial's points" : "Bots take it only when its holder leaves it open");
            popValueRow(c, Rect{116, 178, 408, 44}, 1, "Round 1 trade", freeTrade_ ? "Free refills" : "As printed",
                        freeTrade_ ? "A computer playing Trade refills everyone" : "Trade card as printed in round 1");
            popValueRow(c, Rect{116, 226, 408, 44}, 2, "AI speed", kSpeedNames[speed_], "How fast the computer players' turns are shown");
            popDone(c, Rect{250, 290, 140, 32}, 3);
            c.fillRect(0, 446, 640, 34, rgb(5, 8, 16));
            drawHintBar(c, {{"D-PAD", "Move"}, {"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", popSel_ == 3 ? "Done" : "Change"}, {"B", "Done"}});
        }
    }

    void drawWrapped(Canvas& c, const FontData& f, int x, int y, int w, const std::string& s, uint32_t col) {
        for (const std::string& l : wrapText(f, s, w)) {
            drawText(c, f, x, y, l, col);
            y += f.lineHeight;
        }
    }

    // ------------------------------------------------------------ the faction picker (0.28.2)
    struct PickEntry {
        int faction = -1, tribunii = -1;  // playable ones
        const char* soonKey = nullptr;     // coming soon: the emblem's key
        std::string name;
        int group = 0;                     // 0 playable, 1 Thunder's Edge, 2 Discordant Stars
        int real = -1;                     // 0.43.0: the faction behind a greyed one (its module is off)
        const char* secret = nullptr;      // (0.85.3) a Blue Reverie faction not unlocked yet: its key (shown as "?")
    };
    std::vector<PickEntry> picks_;
    std::vector<int> pickX_, pickY_;  // layout, in content units (y grows down; headers take space)
    int pickH_ = 0;
    bool pickerOpen_ = false;
    bool teOn_ = false;  // Thunder's Edge ticked on the Modules screen
    bool dsOn_ = false;  // Discordant Stars too
    bool brOn_ = false;  // ... and Blue Reverie (0.83.0; 0.85.3: any of its factions unlocked)
    uint32_t brMask_ = 0;  // (0.85.3) the Blue Reverie factions unlocked (Setup::brMask)
    int pickSel_ = 0, pickScroll_ = 0, pickSeat_ = 0;  // pickSeat_: the seat the picker was opened for
    float pickScrollF_ = 0.f;  // the wheel's scroll (smooth), and whether the view follows the chosen one
    bool pickFree_ = false;
    static constexpr int kPickAreaH = 300;
    std::string pickMsg_;
    static constexpr int kPickCols = 8, kCellW = 74, kCellH = 62, kHeadH = 18;

    void buildPicks() {
        if (!picks_.empty()) return;
        picks_.push_back({-1, -1, nullptr, "Random", 0});
        for (const auto& ft : factions_) picks_.push_back({ft.first, ft.second, nullptr, choiceName(ft.first, ft.second), 0});
        // Thunder's Edge (0.35.0): playable once the faction is in and the module is ticked; the rest coming soon
        for (const SoonFaction& sf : kThundersEdge) {
            int f = findFaction(!std::strcmp(sf.key, "lastbastion") ? "bastion" : !std::strcmp(sf.key, "obsidian") ? "firmament" : sf.key);
            if (f >= 0 && teOn_ && factionImplemented(f)) picks_.push_back({f, -1, nullptr, kFactions[f].name, 1});
            else picks_.push_back({-1, -1, sf.key, sf.name, 1, f});
        }
        // Discordant Stars (0.37.0): the same way, with its module
        for (const SoonFaction& sf : kDiscordant) {
            const char* k = sf.key;
            const int f = findFaction(!std::strcmp(k, "ilyxum") ? "augers" : !std::strcmp(k, "ltokk") ? "khrask" : !std::strcmp(k, "myko") ? "mykomentori"
                                      : !std::strcmp(k, "rhodun") ? "zealots" : k);
            if (f >= 0 && dsOn_ && factionImplemented(f)) picks_.push_back({f, -1, nullptr, kFactions[f].name, 2});
            else picks_.push_back({-1, -1, sf.key, sf.name, 2, f});
        }
        // Blue Reverie (0.83.0): the same way, with its module (the ones in this version's level)
        // (0.85.3, owner: "secret unlockables") the ones not earned yet are a "?" - nobody plays them, the bots neither
        for (const SoonFaction& sf : kBlueReverie) {
            const int f = findFaction(sf.key);
            if (f >= 0 && brInPool(f, kBrLevel, brMask_)) picks_.push_back({f, -1, nullptr, kFactions[f].name, 3});
            else {
                PickEntry e;
                e.name = "Secret faction";
                e.group = 3;
                e.secret = sf.key;
                picks_.push_back(e);
            }
        }
        int col = 0, y = kHeadH, group = 0;
        for (const PickEntry& e : picks_) {
            if (e.group != group) {
                group = e.group;
                y += (col ? kCellH : 0) + kHeadH;
                col = 0;
            }
            pickX_.push_back(col * kCellW);
            pickY_.push_back(y);
            if (++col == kPickCols) {
                col = 0;
                y += kCellH;
            }
        }
        pickH_ = y + (col ? kCellH : 0);
    }
    void openPicker() {
        picks_.clear();  // (rebuilt: the Modules choice may have changed)
        pickX_.clear();
        pickY_.clear();
        buildPicks();
        pickerOpen_ = true;
        pickMsg_.clear();
        pickSeat_ = seatSel_;
        const SeatSetup& s = seats_[size_t(pickSeat_)];
        pickSel_ = 0;
        pickFree_ = false;
        for (size_t i = 0; i < picks_.size(); ++i)
            if (picks_[i].group == 0 && picks_[i].faction == s.faction && picks_[i].tribunii == s.tribunii) pickSel_ = int(i);
    }
    bool pickable(const PickEntry& e) const {
        return !e.soonKey && !e.secret && (e.faction < 0 || factionFree(e.faction, pickSeat_, e.tribunii));
    }
    // (0.85.3) a secret Blue Reverie faction: how it is earned (its name stays hidden)
    static std::string secretWhy(const char* key) {
        const int f = findFaction(key);
        if (f < 0 || brBatch(f) <= 0 || !brInPool(f, kBrLevel)) return "A secret Blue Reverie faction - it comes in a later version.";
        const league::LeagueDef* by = league::find(profile::factionLeague(key));
        return std::string("A secret Blue Reverie faction. Finish the ") + (by ? by->name : "its league") + " in the Career to unlock it.";
    }
    void movePick(int dx, int dy) {
        const int n = int(picks_.size());
        if (dx) pickSel_ = std::max(0, std::min(n - 1, pickSel_ + dx));
        if (dy) {
            const int y0 = pickY_[size_t(pickSel_)], x0 = pickX_[size_t(pickSel_)];
            int best = -1, by = 0;
            for (int i = 0; i < n; ++i) {  // the nearest row up / down, then the closest column
                const int y = pickY_[size_t(i)];
                if (dy > 0 ? y <= y0 : y >= y0) continue;
                if (best >= 0 && (dy > 0 ? y > by : y < by)) continue;
                if (best < 0 || y != by || std::abs(pickX_[size_t(i)] - x0) < std::abs(pickX_[size_t(best)] - x0)) {
                    best = i;
                    by = y;
                }
            }
            if (best >= 0) pickSel_ = best;
        }
        pickMsg_.clear();
    }
    void updatePicker(const Input& in) {
        if (in.mouse().wheel != 0.f) {  // free scrolling: the grid moves, the pointer chooses
            if (!pickFree_) pickScrollF_ = float(pickScroll_);
            pickFree_ = true;
            pickScrollF_ = std::max(0.f, std::min(float(std::max(0, pickH_ - kPickAreaH)), pickScrollF_ - in.mouse().wheel * 36.f));
            pickScroll_ = int(pickScrollF_ + 0.5f);
        }
        if (in.pressed(Btn::Left) || in.pressed(Btn::Right) || in.pressed(Btn::Up) || in.pressed(Btn::Down) || in.pressed(Btn::L1) ||
            in.pressed(Btn::R1))
            pickFree_ = false;  // the keys: the view follows the chosen faction again
        if (in.pressed(Btn::Left)) movePick(-1, 0);
        if (in.pressed(Btn::Right)) movePick(1, 0);
        if (in.pressed(Btn::Up)) movePick(0, -1);
        if (in.pressed(Btn::Down)) movePick(0, 1);
        if (in.pressed(Btn::L1)) movePick(-kPickCols * 2, 0);
        if (in.pressed(Btn::R1)) movePick(kPickCols * 2, 0);
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) pickerOpen_ = false;
        if (in.pressed(Btn::A) || in.pressed(Btn::Start)) {
            const PickEntry& e = picks_[size_t(pickSel_)];
            if (e.soonKey) {
                pickMsg_ = e.group == 3 ? (brOn_ ? "This Blue Reverie faction is coming soon."
                                                 : "Tick Blue Reverie on the Modules screen (title screen) to play its factions.")
                           : e.group == 2 ? (dsOn_ ? "This Discordant Stars faction is coming soon."
                                                 : "Tick Discordant Stars on the Modules screen (title screen) to play its factions.")
                           : teOn_      ? "This Thunder's Edge faction is coming soon."
                                        : "Tick Thunder's Edge on the Modules screen (title screen) to play its factions.";
            } else if (e.secret) {
                pickMsg_ = secretWhy(e.secret);
            } else if (!pickable(e)) {
                pickMsg_ = "Another seat already has that faction (or its home).";
            } else {
                SeatSetup& s = seats_[size_t(pickSeat_)];
                s.faction = e.faction;
                s.tribunii = e.tribunii;
                seatSel_ = pickSeat_;
                pickerOpen_ = false;
            }
        }
    }
    void drawPicker(Canvas& c, uint32_t now) {
        c.fillRect(0, 0, 640, 480, rgb(4, 6, 12), 245);
        Rect r{8, 8, 624, 436};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 12, rgb(12, 16, 28));
        drawPanel(c, r, rgb(12, 16, 28), theme::gold, 12);
        drawTextAligned(c, FONT_LG, r.x, r.y + 6, r.w, Align::Center, "Choose a faction - seat " + std::to_string(pickSeat_ + 1), theme::gold);
        const Rect area{r.x + 16, r.y + 34, kPickCols * kCellW, kPickAreaH};
        // keep the chosen one in view (not while the wheel scrolls)
        const int sy = pickY_[size_t(pickSel_)];
        if (!pickFree_ && sy - kHeadH < pickScroll_) pickScroll_ = std::max(0, sy - kHeadH);
        if (!pickFree_ && sy + kCellH > pickScroll_ + area.h) pickScroll_ = sy + kCellH - area.h;
        c.setClip(area);
        const char* kGroup[4] = {"Prophecy of Kings and the Council Keleres",
                                 teOn_ ? "Thunder's Edge" : "Thunder's Edge - tick it on the Modules screen",
                                 dsOn_ ? "Discordant Stars (fan-made)" : "Discordant Stars (fan-made) - tick it on the Modules screen",
                                 brOn_ ? "Blue Reverie (fan-made) - more are unlocked in the Career" : "Blue Reverie (fan-made) - secret factions, unlocked in the Career"};
        int lastGroup = -1;
        for (size_t i = 0; i < picks_.size(); ++i) {
            const PickEntry& e = picks_[i];
            const int x = area.x + pickX_[i], y = area.y + pickY_[i] - pickScroll_;
            if (e.group != lastGroup) {
                lastGroup = e.group;
                const int hy = y - kHeadH + 2;
                if (hy > area.y - kHeadH && hy < area.y + area.h)
                    drawText(c, FONT_SMB, area.x, hy, kGroup[e.group],
                             e.group == 0 || (e.group == 1 && teOn_) || (e.group == 2 && dsOn_) || (e.group == 3 && brOn_) ? theme::gold
                                                                                                                         : rgb(150, 150, 170));
            }
            if (y + kCellH < area.y || y > area.y + area.h) continue;
            const bool sel = int(i) == pickSel_, ok = pickable(e);
            const Rect cr{x + 2, y + 2, kCellW - 4, kCellH - 4};
            if (y >= area.y && y + kCellH <= area.y + area.h) hits::add(cr, [this, i]() { pickSel_ = int(i); });
            c.fillRoundRect(cr.x, cr.y, cr.w, cr.h, 6, sel ? rgb(34, 44, 72) : rgb(20, 26, 42), 235);
            if (sel) {
                const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
                c.frameRoundRect(cr.x, cr.y, cr.w, cr.h, 6, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
            }
            const int is = 32, ix = cr.x + (cr.w - is) / 2, iy = cr.y + 3;
            const int a = e.soonKey ? 110 : ok ? 255 : 90;
            if (e.soonKey) icons::draw(c, std::string("faction/") + e.soonKey, ix, iy, is, a);
            else if (e.faction >= 0) drawFactionIcon(c, e.faction, ix, iy, is, a);
            else drawTextAligned(c, FONT_LG, cr.x, iy + 6, cr.w, Align::Center, "?", e.secret ? rgb(150, 120, 220) : theme::dim);
            std::string nm = e.name;
            if (nm.compare(0, 4, "The ") == 0) nm = nm.substr(4);
            if (!e.soonKey && e.faction >= 0 && e.tribunii < 0) nm = factionShortName(e.faction);
            if (e.tribunii >= 0) nm = std::string("Keleres/") + factionShortName(e.tribunii);
            const auto lines = wrapText(FONT_XS, nm, cr.w - 4);
            for (size_t k = 0; k < lines.size() && k < 2; ++k)
                drawTextAligned(c, FONT_XS, cr.x, cr.y + 37 + int(k) * 10, cr.w, Align::Center, lines[k],
                                e.soonKey ? rgb(110, 112, 128) : ok ? theme::text : theme::faint);
        }
        c.resetClip();
        // (0.81.1, owner) a slim scroll bar beside the grid
        drawScrollBar(c, Rect{area.x + area.w + 3, area.y, 4, area.h}, pickScroll_, area.h, pickH_, [this](int dir) {
            if (!pickFree_) pickScrollF_ = float(pickScroll_);
            pickFree_ = true;
            pickScrollF_ = std::max(0.f, std::min(float(std::max(0, pickH_ - kPickAreaH)), pickScrollF_ + float(dir * (kPickAreaH - 40))));
            pickScroll_ = int(pickScrollF_ + 0.5f);
        });
        if (pickScroll_ > 0) drawTextAligned(c, FONT_XS, r.x, area.y - 12, r.w - 20, Align::Right, "\xE2\x96\xB2 more", theme::dim);
        if (pickScroll_ + area.h < pickH_)
            drawTextAligned(c, FONT_XS, r.x, area.y + area.h + 1, r.w - 20, Align::Right, "\xE2\x96\xBC more", theme::dim);
        // the chosen one, with a line about it
        const PickEntry& e = picks_[size_t(pickSel_)];
        const int dy = area.y + area.h + 14;
        c.fillRoundRect(r.x + 12, dy, r.w - 24, 70, 8, rgb(18, 24, 40));
        if (e.soonKey) icons::draw(c, std::string("faction/") + e.soonKey, r.x + 20, dy + 7, 56, 170);
        else if (e.faction >= 0) drawFactionIcon(c, e.faction, r.x + 20, dy + 7, 56);
        else if (e.secret) drawTextAligned(c, FONT_XL, r.x + 20, dy + 16, 56, Align::Center, "?", rgb(150, 120, 220));
        const int tx = r.x + 86;
        if (e.secret) {  // (0.85.3) a secret Blue Reverie faction: no name, how to earn it
            drawText(c, FONT_MDB, tx, dy + 6, "Secret faction", rgb(170, 150, 230));
            int sy = dy + 26;
            for (const std::string& l : wrapText(FONT_SM, pickMsg_.empty() ? secretWhy(e.secret) : pickMsg_, r.w - 110)) {
                drawText(c, FONT_SM, tx, sy, l, theme::dim);
                sy += 15;
            }
            drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Take it"}, {"L1/R1", "Page"}, {"B", "Back"}});
            return;
        }
        drawText(c, FONT_MDB, tx, dy + 6, !e.soonKey ? choiceName(e.faction, e.tribunii) : e.name, !e.soonKey ? theme::gold : rgb(170, 170, 190));
        const int shown = e.soonKey ? e.real : e.faction;  // a greyed faction: still its summary (0.43.0)
        if (const GuideBlurb* gb = pickMsg_.empty() ? guideBlurbOf(shown) : nullptr) {
            // difficulty and style (0.43.0): a coloured word, then the tags, at the right of the name line
            const char* d = gb->difficulty;
            const bool beg = !std::strncmp(d, "BEG", 3), inter = !std::strncmp(d, "INT", 3), adv = !std::strncmp(d, "ADV", 3);
            const char* word = beg ? "Beginner-friendly" : inter ? "Intermediate" : adv ? "Advanced" : "Expert";
            const uint32_t col = beg ? rgb(120, 210, 130) : inter ? theme::gold : adv ? rgb(240, 150, 70) : rgb(235, 90, 90);
            const int right = r.x + r.w - 22;
            const int ww = textWidth(FONT_XS, word);
            drawTextAligned(c, FONT_XS, right - ww, dy + 9, ww, Align::Right, word, col);
            const int room = right - ww - 12 - (tx + textWidth(FONT_MDB, !e.soonKey ? choiceName(e.faction, e.tribunii) : e.name) + 10);
            std::string tags = gb->tags;  // as many tags as fit beside the name
            while (textWidth(FONT_XS, tags) > room && tags.rfind(", ") != std::string::npos) tags.resize(tags.rfind(", "));
            const int tw = textWidth(FONT_XS, tags);
            if (tw <= room) drawText(c, FONT_XS, right - ww - 10 - tw, dy + 9, tags, theme::dim);
        }
        const std::string text = !pickMsg_.empty()       ? pickMsg_
                                 : e.soonKey && shown < 0 ? std::string("Coming soon.")
                                                          : std::string(blurbOf(shown));
        int ly = dy + 26;
        auto lines = wrapText(FONT_SM, text, r.w - 110);
        if (lines.size() > 3) {  // the box holds three lines (0.43.0: the guide summaries are longer)
            lines.resize(3);
            std::string& last = lines[2];
            while (!last.empty() && textWidth(FONT_SM, last + "...") > r.w - 110) last.resize(last.rfind(' ') == std::string::npos ? 0 : last.rfind(' '));
            last += "...";
        }
        for (const std::string& l : lines) {
            drawText(c, FONT_SM, tx, ly, l, !pickMsg_.empty() ? theme::red : e.soonKey ? theme::dim : theme::text);
            ly += 15;
        }
        drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Take it"}, {"L1/R1", "Page"}, {"B", "Back"}});
    }

    void drawMenu(Canvas& c) {
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 200);
        Rect r{8, 8, 624, 428};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 12, rgb(12, 16, 28));
        drawPanel(c, r, theme::panel, theme::gold, 12);
        drawTextAligned(c, FONT_LG, r.x, r.y + 8, r.w, Align::Center, "Factions and seats", theme::gold);
        // columns: seat, played by, faction, colour, difficulty, personality
        static const int kX[kCols + 1] = {58, 150, 330, 418, 526, 620};
        static const char* kHead[kCols] = {"Played by", "Faction", "Colour", "Difficulty", "Personality"};
        int y = r.y + 40;
        drawText(c, FONT_XS, r.x + 12, y, "Seat", theme::faint);
        for (int k = 0; k < kCols; ++k) drawText(c, FONT_XS, kX[k] + 4, y, kHead[k], colSel_ == k ? theme::gold : theme::faint);
        y += 16;
        const int rowH = players_ > 6 ? 34 : 40;
        const int bottom = std::max(0, southernmostSeat(players_, layout()));
        const int me = primary();
        int personNo = 1;
        for (int i = 0; i < players_; ++i) {
            const SeatSetup& s = seats_[size_t(i)];
            const bool rowSel = i == seatSel_ && !onConfirm_;
            if (rowSel) c.fillRoundRect(r.x + 6, y, r.w - 12, rowH - 3, 6, theme::panelHi);
            const int ty = y + (rowH - 3) / 2 - FONT_SMB.lineHeight / 2;
            drawText(c, FONT_SMB, r.x + 12, ty - (i == bottom ? 6 : 0), std::to_string(i + 1), theme::dim);
            if (i == bottom) drawText(c, FONT_XS, r.x + 8, ty + 9, "bottom", theme::faint);
            for (int col = 0; col < kCols; ++col)  // the mouse (0.27.0): point at a cell, click to change it
                hits::add(Rect{kX[col], y, kX[col + 1] - kX[col] - 4, rowH - 3}, [this, i, col]() {
                    seatSel_ = i;
                    colSel_ = col;
                    onConfirm_ = false;
                });
            auto cell = [&](int col, const std::string& text, uint32_t colour, int iconFaction = -1) {
                const int x = kX[col], w = kX[col + 1] - kX[col] - 4;
                if (rowSel && colSel_ == col) c.frameRoundRect(x, y + 2, w, rowH - 7, 5, theme::gold, 2);
                int tx = x + 6;
                if (iconFaction >= 0) {
                    drawFactionIcon(c, iconFaction, tx, y + (rowH - 3) / 2 - 11, 22);
                    tx += 26;
                }
                drawText(c, FONT_SMB, tx, ty, fitText(FONT_SMB, text, x + w - tx - 4), colour);
            };
            std::string who = !s.human ? "Computer" : i == me ? "You" : "Person " + std::to_string(++personNo);
            cell(kWho, who, s.human ? theme::text : theme::dim);
            cell(kFaction, choiceName(s.faction, s.tribunii), s.faction < 0 ? theme::dim : theme::gold, s.faction);
            {
                const int x = kX[kColour];
                if (rowSel && colSel_ == kColour) c.frameRoundRect(x, y + 2, kX[kColour + 1] - x - 4, rowH - 7, 5, theme::gold, 2);
                if (s.color >= 0) {
                    {
                        pcolours::Finish fin(c, pcolours::around(float(x + 16), float(y + (rowH - 3) / 2), 9.f, 9.f), s.color, uint32_t(i) * 5u);
                        c.fillCircle(float(x + 16), float(y + (rowH - 3) / 2), 8.f, pcolours::base(s.color));
                    }
                    drawText(c, FONT_SM, x + 28, ty + 1, fitText(FONT_SM, pcolours::info(s.color).brief, kX[kColour + 1] - x - 32), theme::text);
                } else {
                    drawText(c, FONT_SMB, x + 6, ty, "Auto", theme::dim);
                }
            }
            if (s.human) {
                cell(kLevel, "-", theme::faint);
                cell(kPers, "-", theme::faint);
            } else {
                cell(kLevel, kLevels[s.level], levelColour(s.level));
                if (s.level >= kAiLevelAI) cell(kPers, "None", theme::faint);
                else if (const league::RivalDef* r = s.commander.empty() ? nullptr : league::rival(s.commander)) cell(kPers, r->name, theme::gold);
                else cell(kPers, s.arch < 0 ? "Random" : kArchetypes[s.arch].name, s.arch < 0 ? theme::dim : theme::text);
            }
            y += rowH;
        }
        // the Confirm button (0.62.1), above the line that says what the selection means
        {
            const Rect b{r.x + r.w / 2 - 90, r.y + r.h - 86, 180, 30};
            hits::add(b, [this] { onConfirm_ = true; });  // (pointing at it selects it; a click confirms)
            const float pulse = 0.5f + 0.5f * std::sin(float(pcolours::now() % 1500) / 1500.f * 6.2831853f);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 7, onConfirm_ ? rgb(40, 76, 44) : rgb(22, 40, 28), 245);
            c.frameRoundRect(b.x, b.y, b.w, b.h, 7, onConfirm_ ? mix(theme::goldDim, theme::gold, int(pulse * 255)) : theme::green, onConfirm_ ? 2 : 1);
            drawTextAligned(c, FONT_MDB, b.x, b.y + (b.h - FONT_MDB.lineHeight) / 2, b.w, Align::Center, "Confirm", onConfirm_ ? theme::text : theme::green);
        }
        // what the selected cell means
        const SeatSetup& s = seats_[size_t(seatSel_)];
        std::string info;
        switch (onConfirm_ ? kCols : Col(colSel_)) {
            case kWho:
                info = s.human ? (seatSel_ == me ? "You play this seat."
                                                 : "Another person plays this seat at the same device (hot-seat): the game asks you to pass it over before their turns.")
                               : "The computer plays this seat.";
                break;
            case kFaction:
                if (s.faction < 0) info = "A random faction (never one another seat picked).";
                else if (s.faction == findFaction("keleres"))
                    info = std::string("The Council Keleres: they take the home system (and the matching hero) of ") +
                           (s.tribunii < 0 ? "the Mentak, the Xxcha or the Argent Flight - one not in the game." : std::string(kFactions[s.tribunii].name) + ".");
                else info = std::string(kFactions[s.faction].name) + ".";
                break;
            case kColour:
                info = s.color < 0                       ? "Auto: the next of the classic eight colours nobody picked."
                       : s.color >= pcolours::kClassic ? std::string(pcolours::info(s.color).name) + ": " + pcolours::info(s.color).look + "."
                                                        : "Each colour can be used by one seat only.";
                break;
            case kLevel: info = s.human ? "Difficulty is for computer players." : kLevelInfo[s.level]; break;
            case kPers:
                info = s.human ? "Personalities are for computer players."
                       : s.level >= kAiLevelAI ? "The A.I. has no personality: no archetype, grudges or betrayals. Every move is chosen to win."
                       : !s.commander.empty() && league::rival(s.commander) ? commanderInfo(*league::rival(s.commander))
                               : s.arch < 0 ? (s.level == 3 ? "Random: any archetype but Rusher and Chaotic (Competitive bots). Hidden until the end of round 3."
                                                            : "Random: any of the ten archetypes, Chaotic included. Hidden until the end of round 3.")
                                            : std::string(kArchetypes[s.arch].blurb) + ".";
                break;
            default: break;
        }
        if (onConfirm_) info = "Keep these seats and go back to the New Game screen.";
        auto lines = wrapText(FONT_SM, info, r.w - 40);
        int yy = r.y + r.h - 12 - int(std::min<size_t>(2, lines.size())) * 16;
        for (size_t k = 0; k < lines.size() && k < 2; ++k) {
            drawTextAligned(c, FONT_SM, r.x, yy, r.w, Align::Center, lines[k], theme::text);
            yy += 16;
        }
        if (onConfirm_) drawHintBar(c, {{"D-PAD", "Move"}, {"A", "Confirm"}, {"B", "Done"}});
        else drawHintBar(c, {{"D-PAD", "Move"}, {"A", "Choose"}, {"Y", "Previous"}, {"X", "Random"}, {"B", "Done"}});
    }

    // ---------------------------------------------------------------- 0.61.4: the option windows
    // Owner's document: "When a color, difficulty, or personality is to be chosen, have a small window open over the respective field
    // to allow the player to choose from a menu of their options." A on the cell (or a click) opens it under the cell; the D-pad / the
    // mouse point, A (a click) takes one, B (or a click beside it) closes it. Taken colours are greyed with the seat that has them.
    // (0.80.11) a rival commander, in a line or two
    static std::string commanderInfo(const league::RivalDef& r) {
        const int f = r.faction && *r.faction ? findFaction(r.faction) : -1;
        const int g = leaguerun::grudgeOf(r.id);
        std::string s = std::string(r.name) + ": " + (r.arch >= 0 ? kArchetypes[r.arch].name : "a hidden personality") +
                        (f >= 0 ? std::string(", plays ") + factionShortName(f) : std::string()) + ".";
        s += g > 0 ? " Remembers your Career games - and holds a grudge." : " Remembers your Career games.";
        return s;
    }
    struct Opt {
        int value = 0;
        std::string label, sub;
        bool enabled = true;
        int swatch = -1;     // a colour's index
        std::string detail;  // (colours) what it is, under the grid
        bool lockedSwatch = false;  // (0.80.0) not earned yet: still highlighted to read why
    };
    void openOpt() {
        opts_.clear();
        const SeatSetup& s = seats_[size_t(seatSel_)];
        int cur = 0;
        optCol_ = colSel_;
        if (colSel_ == kColour) {
            opts_.push_back({-1, "Auto", "", true, -1, "The next of the classic eight colours nobody picked."});
            for (int c = 0; c < pcolours::kCount; ++c) {  // (0.62.0: index 1..8 the classic colours, then the new ones; 0.75.0: 24)
                Opt o{c, pcolours::info(c).name, "", colourFree(c, seatSel_), c,
                      c >= pcolours::kClassic ? std::string(pcolours::info(c).look) + "." : "One of the classic colours."};
                if (!profile::colourUnlocked(c)) {  // 0.80.0: locked until its league is finished
                    o.sub = "locked";
                    o.detail = std::string(pcolours::info(c).look) + ". Locked: " + profile::colourSource(c) + " to earn it.";
                    o.lockedSwatch = true;
                }
                if (!o.enabled)
                    for (int i = 0; i < players_; ++i)
                        if (i != seatSel_ && seats_[size_t(i)].color == c) o.sub = "seat " + std::to_string(i + 1);
                opts_.push_back(o);
            }
            for (size_t k = 0; k < opts_.size(); ++k)
                if (opts_[k].value == s.color) cur = int(k);
            optLastCol_ = s.color >= 0 ? s.color / pcolours::kClassic : 0;
        } else if (colSel_ == kLevel) {
            for (int l = 0; l < kLevelCount; ++l) opts_.push_back({l, kLevels[l], kLevelInfo[l], true, -1});
            cur = s.level;
        } else {
            opts_.push_back({-1, "Random", s.level == 3 ? "Any but Rusher and Chaotic (Competitive bots)" : "Any of the ten", true, -1});
            for (int a = 0; a < int(Arch::Count); ++a) opts_.push_back({a, kArchetypes[a].name, kArchetypes[a].blurb, true, -1});
            // (0.80.11) the rival commanders you have beaten in the Career - none listed until you have (a hidden unlock)
            commanders_ = leaguerun::commanders();
            if (!commanders_.empty()) {
                Opt head{kCommanderHead, "Rival commanders", "", false, -1};
                opts_.push_back(head);
                for (size_t k = 0; k < commanders_.size(); ++k) {
                    const league::RivalDef& r = *commanders_[k];
                    Opt o{kCommanderBase + int(k), r.name, commanderInfo(r), true, -1};
                    for (int i = 0; i < players_; ++i)
                        if (i != seatSel_ && seats_[size_t(i)].commander == r.id) o.enabled = false, o.detail = "seat " + std::to_string(i + 1);
                    opts_.push_back(o);
                }
            }
            for (size_t k = 0; k < opts_.size(); ++k)
                if (s.commander.empty() ? opts_[k].value == s.arch
                                        : opts_[k].value >= kCommanderBase && commanders_[size_t(opts_[k].value - kCommanderBase)]->id == s.commander)
                    cur = int(k);
        }
        optSel_ = cur;
        optSeat_ = seatSel_;
        optOpen_ = true;
    }
    // (0.80.0) a locked colour can be highlighted - to read how it is earned - but not taken
    bool seeable(int k) const { return k >= 0 && k < int(opts_.size()) && (opts_[size_t(k)].enabled || opts_[size_t(k)].lockedSwatch); }
    void takeOpt() {
        const Opt& o = opts_[size_t(optSel_)];
        if (!o.enabled) return;
        SeatSetup& s = seats_[size_t(optSeat_)];
        if (optCol_ == kColour) s.color = o.value;
        else if (optCol_ == kLevel) s.level = o.value;
        else if (o.value >= kCommanderBase) s.commander = commanders_[size_t(o.value - kCommanderBase)]->id, s.arch = commanders_[size_t(o.value - kCommanderBase)]->arch;
        else s.arch = o.value, s.commander.clear();
        optOpen_ = false;
        audio::play(audio::Sfx::Confirm);
    }
    void updateOpt(const Input& in) {
        const int n = int(opts_.size());
        auto step = [&](int d) {
            for (int k = 0; k < n; ++k) {
                optSel_ = (optSel_ + d + n) % n;
                if (opts_[size_t(optSel_)].enabled) break;
            }
        };
        if (optCol_ == kColour) {  // (0.62.0) Auto on top, then columns of eight: the classic colours, then the new ones (0.75.0: 3)
            constexpr int kRows = pcolours::kClassic, kColourCols = pcolours::kCount / pcolours::kClassic;
            auto colOf = [](int k) { return k <= 0 ? 0 : (k - 1) / kRows; };
            auto rowOf = [](int k) { return k == 0 ? -1 : (k - 1) % kRows; };
            auto idx = [](int col, int row) { return row < 0 ? 0 : 1 + col * kRows + row; };
            const int cur = optSel_;
            int col = cur == 0 ? optLastCol_ : colOf(cur), row = rowOf(cur);
            auto vert = [&](int d) {
                for (int k = 0; k < kRows + 1; ++k) {
                    row += d;
                    if (row >= kRows) row = -1;
                    if (row < -1) row = kRows - 1;
                    if (seeable(idx(col, row))) return;
                }
            };
            if (in.pressed(Btn::Up)) vert(-1);
            if (in.pressed(Btn::Down)) vert(1);
            if ((in.pressed(Btn::Left) || in.pressed(Btn::Right)) && row >= 0) {
                const int other = (col + (in.pressed(Btn::Right) ? 1 : kColourCols - 1)) % kColourCols;
                for (int d = 0; d < kRows; ++d) {  // the same row in the other column, or the nearest free one
                    const int r1 = row - d, r2 = row + d;
                    if (r1 >= 0 && seeable(idx(other, r1))) { col = other, row = r1; break; }
                    if (r2 < kRows && seeable(idx(other, r2))) { col = other, row = r2; break; }
                }
            }
            optSel_ = idx(col, row);
            if (row >= 0) optLastCol_ = col;
        } else {
            if (in.pressed(Btn::Up)) step(-1);
            if (in.pressed(Btn::Down)) step(1);
        }
        if (in.pressed(Btn::A) || in.pressed(Btn::Start)) takeOpt();
        else if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) optOpen_ = false;
    }
    // the window, under the cell it was opened from (above it when there is no room)
    void drawOpt(Canvas& c, uint32_t now) {
        static const int kX[kCols + 1] = {58, 150, 330, 418, 526, 620};
        const bool pers = optCol_ == kPers, colour = optCol_ == kColour;
        const int rowH = colour ? 22 : pers ? 22 : 36;
        const int w = colour ? 466 : pers ? 236 : 292;  // (0.75.0: three columns of colours)
        const int detailH = pers ? 46 : colour ? 32 : 0;
        const int shown = pers ? std::min(int(opts_.size()), kPersRows) : int(opts_.size());  // (0.80.11: commanders scroll)
        const int h = colour ? 28 + 24 + 14 + rowH * pcolours::kClassic + detailH + 6 : 30 + rowH * shown + detailH + 8;
        const int rowsTop = 56, rowsH = players_ > 6 ? 34 : 40;
        const int cellY = 8 + rowsTop + optSeat_ * rowsH;
        int x = std::min(640 - w - 6, std::max(6, kX[optCol_] - (w - (kX[optCol_ + 1] - kX[optCol_])) / 2));
        int y = cellY + rowsH;
        if (y + h > 446) y = std::max(6, cellY - h - 2);
        if (y + h > 446) y = std::max(6, 446 - h);
        if (y < cellY + rowsH && y + h > cellY) {  // no room above or below: beside the cell (left of its column), level with it
            x = kX[optCol_] - w - 6 >= 6 ? kX[optCol_] - w - 6 : std::min(640 - w - 6, kX[optCol_ + 1] + 6);
            y = std::min(446 - h, std::max(6, cellY + rowsH / 2 - h / 2));
        }
        hits::clear();  // (the table under the window doesn't take the mouse)
        hits::addBackdrop(Rect{0, 0, 640, 446}, [this] { optOpen_ = false; });  // (a click beside it closes it)
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 90);
        c.fillRect(x + 4, y + 5, w, h, rgb(0, 0, 0), 120);
        drawPanel(c, Rect{x, y, w, h}, rgb(12, 16, 30), theme::gold, 10, 250);
        hits::addBlock(Rect{x, y, w, h});
        const char* title = colour ? "Colour" : pers ? "Personality" : "Difficulty";
        drawText(c, FONT_SMB, x + 12, y + 7, std::string(title) + " - seat " + std::to_string(optSeat_ + 1), theme::gold);
        int ry = y + 28;
        if (colour) {  // (0.62.0) Auto across the top, the classic colours on the left, the new ones beside them (0.75.0: two columns)
            constexpr int kColourCols = pcolours::kCount / pcolours::kClassic;
            const int colW = (w - 12 - 6 * (kColourCols - 1)) / kColourCols;
            auto cellOf = [&](size_t k) {
                if (k == 0) return Rect{x + 6, y + 28, w - 12, 22};
                const int col = (int(k) - 1) / pcolours::kClassic, row = (int(k) - 1) % pcolours::kClassic;
                return Rect{x + 6 + col * (colW + 6), y + 28 + 24 + 14 + row * rowH, colW, rowH - 2};
            };
            drawText(c, FONT_XS, x + 14, y + 28 + 24 + 1, "Classic", theme::faint);
            drawText(c, FONT_XS, x + 12 + colW + 8, y + 28 + 24 + 1, "New", theme::faint);
            for (size_t k = 0; k < opts_.size(); ++k) {
                const Opt& o = opts_[k];
                const bool sel = int(k) == optSel_;
                const Rect rr = cellOf(k);
                if (o.enabled || o.lockedSwatch) hits::add(rr, [this, k] { optSel_ = int(k); });
                if (sel) {
                    const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
                    c.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 5, rgb(34, 44, 72), 240);
                    c.frameRoundRect(rr.x, rr.y, rr.w, rr.h, 5, mix(theme::goldDim, theme::gold, int(pulse * 255)), 1);
                }
                const uint32_t tc = !o.enabled ? theme::faint : sel ? theme::text : rgb(190, 196, 212);
                int tx = rr.x + 8;
                if (o.swatch >= 0) {
                    {
                        pcolours::Finish fin(c, pcolours::around(float(tx + 7), float(rr.y + rr.h / 2), 8.f, 8.f), o.enabled ? o.swatch : -1, uint32_t(k) * 7u);
                        c.fillCircle(float(tx + 7), float(rr.y + rr.h / 2), 7.f, pcolours::base(o.swatch), o.enabled ? 255 : 90);
                    }
                    c.ring(float(tx + 7), float(rr.y + rr.h / 2), 7.5f, 1.f, rgb(10, 10, 16));
                    if (o.lockedSwatch) {  // (0.80.0) a small padlock on it
                        const float px = float(tx + 7), py = float(rr.y + rr.h / 2);
                        c.ring(px, py - 2.f, 2.6f, 1.2f, rgb(210, 214, 228));
                        c.fillRoundRect(int(px - 3.5f), int(py - 1.f), 7, 6, 1, rgb(210, 214, 228));
                    }
                    tx += 20;
                }
                const int subW = o.sub.empty() ? 0 : textWidth(FONT_XS, o.sub) + 6, room = rr.x + rr.w - tx - 4 - subW;
                const FontData& lf = textWidth(FONT_SMB, o.label) <= room ? FONT_SMB : FONT_SM;  // (Luminescent Green: a size down)
                drawText(c, lf, tx, rr.y + (&lf == &FONT_SMB ? 3 : 4), fitText(lf, o.label, room), tc);
                if (!o.sub.empty()) drawTextAligned(c, FONT_XS, rr.x, rr.y + 6, rr.w - 6, Align::Right, o.sub, theme::faint);
            }
            const int dy = y + 28 + 24 + 14 + rowH * pcolours::kClassic + 2;  // what the highlighted one is
            c.hline(x + 10, x + w - 10, dy, theme::border);
            if (optSel_ >= 0 && optSel_ < int(opts_.size())) {
                const Opt& o = opts_[size_t(optSel_)];
                int ly = dy + 4;
                for (const std::string& l : wrapText(FONT_XS, o.detail, w - 24)) {
                    if (ly > y + h - 12) break;
                    drawText(c, FONT_XS, x + 12, ly, l, theme::dim);
                    ly += 13;
                }
            }
            c.fillRect(0, 446, 640, 34, rgb(5, 8, 16));
            drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Take it"}, {"B", "Back"}});
            return;
        }
        const int first = shown < int(opts_.size()) ? std::max(0, std::min(int(opts_.size()) - shown, optSel_ - shown / 2)) : 0;
        if (first > 0) drawTextAligned(c, FONT_XS, x, y + 9, w - 12, Align::Right, "\xE2\x96\xB2 more", theme::faint);
        // (0.81.1, owner) a slim scroll bar when some choices are hidden
        drawScrollBar(c, Rect{x + w - 5, ry, 3, rowH * shown - 2}, first, shown, int(opts_.size()), [this, shown](int dir) {
            const int n = int(opts_.size());
            optSel_ = std::max(0, std::min(n - 1, optSel_ + dir * std::max(1, shown - 1)));
            for (int k = 0; k < n && (!opts_[size_t(optSel_)].enabled || opts_[size_t(optSel_)].value == kCommanderHead); ++k)
                optSel_ = std::max(0, std::min(n - 1, optSel_ + (dir > 0 ? 1 : -1)));
        });
        for (size_t k = size_t(first); k < opts_.size() && int(k) < first + shown; ++k) {
            const Opt& o = opts_[k];
            const bool sel = int(k) == optSel_;
            const Rect rr{x + 6, ry, w - 12, rowH - 2};
            if (pers && o.value == kCommanderHead) {  // (0.80.11) the commanders' heading
                c.hline(x + 10, x + w - 10, ry + 4, theme::border);
                drawText(c, FONT_XS, rr.x + 8, ry + 7, o.label, theme::gold);
                ry += rowH;
                continue;
            }
            if (o.enabled) hits::add(rr, [this, k] { optSel_ = int(k); });
            if (sel) {
                const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
                c.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 5, rgb(34, 44, 72), 240);
                c.frameRoundRect(rr.x, rr.y, rr.w, rr.h, 5, mix(theme::goldDim, theme::gold, int(pulse * 255)), 1);
            }
            const uint32_t tc = !o.enabled ? theme::faint : sel ? theme::text : rgb(190, 196, 212);
            int tx = rr.x + 8;
            if (colour) {
                if (o.swatch >= 0) {
                    c.fillCircle(float(tx + 7), float(rr.y + rr.h / 2), 7.f, pcolours::base(o.swatch), o.enabled ? 255 : 90);
                    c.ring(float(tx + 7), float(rr.y + rr.h / 2), 7.5f, 1.f, rgb(10, 10, 16));
                }
                tx += 22;
                drawText(c, FONT_SMB, tx, rr.y + 3, o.label, tc);
                if (!o.sub.empty())
                    drawTextAligned(c, FONT_XS, rr.x, rr.y + 6, rr.w - 8, Align::Right, o.enabled && o.swatch < 0 ? "" : o.sub, theme::faint);
            } else if (pers) {
                const bool cmd = o.value >= kCommanderBase;
                drawText(c, FONT_SMB, tx, rr.y + 2, fitText(FONT_SMB, o.label, rr.w - 16 - (o.detail.empty() ? 0 : 50)),
                         cmd && o.enabled ? (sel ? theme::gold : theme::goldDim) : tc);
                if (!o.detail.empty()) drawTextAligned(c, FONT_XS, rr.x, rr.y + 5, rr.w - 8, Align::Right, o.detail, theme::faint);
            } else {
                drawText(c, FONT_SMB, tx, rr.y + 2, o.label, o.value == 3 && o.enabled ? (sel ? theme::gold : theme::goldDim) : tc);
                drawText(c, FONT_XS, tx, rr.y + 19, fitText(FONT_XS, o.sub, rr.w - 16), sel ? theme::dim : theme::faint);
            }
            ry += rowH;
        }
        if (pers && optSel_ >= 0 && optSel_ < int(opts_.size())) {  // the highlighted personality, in a line or two
            c.hline(x + 10, x + w - 10, ry + 2, theme::border);
            int ly = ry + 6;
            for (const std::string& l : wrapText(FONT_XS, opts_[size_t(optSel_)].sub, w - 24)) {
                if (ly > y + h - 12) break;
                drawText(c, FONT_XS, x + 12, ly, l, theme::dim);
                ly += 13;
            }
        }
        c.fillRect(0, 446, 640, 34, rgb(5, 8, 16));  // (the table's own hints don't show through)
        drawHintBar(c, {{"\xE2\x96\xB2\xE2\x96\xBC", "Choose"}, {"A", "Take it"}, {"B", "Back"}});
    }
    std::vector<Opt> opts_;
    // (0.80.11) the personality picker's rival commanders: rows kCommanderBase + k, under a heading row; 14 rows show at once
    static constexpr int kCommanderHead = -100, kCommanderBase = 1000, kPersRows = 14;
    std::vector<const league::RivalDef*> commanders_;
    bool optOpen_ = false;
    int optSel_ = 0, optSeat_ = 0, optCol_ = 0, optLastCol_ = 0;

private:
    std::vector<std::pair<int, int>> factions_;  // (faction, Keleres home: -1 = random)
    bool freeTrade_ = true, draft_ = true;
    bool contested_ = true;  // Mecatol Rex: Contested / Calm (0.54.0, Setup::extras bit 128)
    bool hyperlanes_ = true; // Hyperlanes: On / Off (0.58.0, Setup::layout 0 / 1)
    int galaxy_ = 1;              // 0 Random, 1 Draft, 2 Seeded (0.43.0)
    uint32_t mapSeed_ = 1;
    bool fair_ = true;  // (0.63.0) Galaxy: "Fair slices" - off = Setup::unbalanced
    int modules_ = 1;
    // the victory point goals offered: 10, 12 (0.82.6, owner 2026-11-23: "we have 12 point options for league; can we add 12 points as
    // an option for casual games too?") and 14
    static int nextVp(int vp, int step) {
        static const int kGoals[] = {10, 12, 14};
        int i = 0;
        while (i < 3 && kGoals[i] != vp) ++i;
        return kGoals[((i < 3 ? i : 0) + (step > 0 ? 1 : 2)) % 3];
    }
    int sel_ = kStart, players_ = 6, vp_ = 10, speed_ = 1, myColor_ = 0;
    bool first_ = true;
    bool menuOpen_ = false;
    int seatSel_ = 0, colSel_ = kFaction;
    bool onConfirm_ = false;  // (0.62.1) the Confirm button has the focus
    std::array<SeatSetup, 8> seats_{};
    std::string error_;
};

}  // namespace

std::unique_ptr<Scene> makeNewGameScene() { return std::unique_ptr<Scene>(new NewGameScene()); }
