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

using namespace ti;

std::unique_ptr<Scene> makeGameScene(std::unique_ptr<GameSession> session);

namespace {

const char* kSpeedNames[4] = {"Slow", "Normal", "Fast", "Instant"};
const char* kLevels[4] = {"Easy", "Normal", "Hard", "Competitive"};
const char* kLevelInfo[4] = {"Relaxed opponents.", "They plan for objectives.", "They play to win by rounds 6-8.",
                             "Sharpest play; personality habits toned down."};

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

struct SeatSetup {
    bool human = false;
    int faction = -1;  // index into kFactions, -1 = random
    int tribunii = -1; // the Council Keleres: whose home they take (Mentak / Xxcha / Argent), -1 = random
    int color = -1;    // -1 = the next free colour
    int level = 3;     // computer difficulty (Competitive by default - owner)
    int arch = -1;     // computer personality, -1 = random
};

class NewGameScene : public Scene {
public:
    NewGameScene() {
        std::vector<int> fs;
        for (int f = 0; f < kFactionCount; ++f)
            if (kFactions[f].source != Source::ThundersEdge && kFactions[f].source != Source::Discordant)
                fs.push_back(f);  // (Thunder's Edge, Discordant Stars: their own groups)
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

    enum Row { kPlayers, kSeats, kVP, kTrade, kGalaxy, kHyperlanes, kMecatol, kSpeed, kStart, kRows };
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
            case kColour: {  // Auto, then the eight colours, skipping taken ones
                int pos = s.color + 1;
                for (int tries = 0; tries < 9; ++tries) {
                    pos = ((pos + (step > 0 ? 1 : -1)) % 9 + 9) % 9;
                    const int c = pos - 1;
                    if (c < 0 || colourFree(c, seatSel_)) {
                        s.color = c;
                        break;
                    }
                }
                break;
            }
            case kLevel:
                if (!s.human) s.level = ((s.level + (step > 0 ? 1 : -1)) % 4 + 4) % 4;
                break;
            case kPers:
                if (!s.human) {
                    const int m = int(Arch::Count) + 1;  // Random + the archetypes
                    s.arch = ((s.arch + 1 + (step > 0 ? 1 : -1)) % m + m) % m - 1;
                }
                break;
            default: break;
        }
    }
    void updateMenu(const Input& in) {
        if (in.pressed(Btn::Up)) seatSel_ = (seatSel_ + players_ - 1) % players_;
        if (in.pressed(Btn::Down)) seatSel_ = (seatSel_ + 1) % players_;
        if (in.pressed(Btn::Left)) colSel_ = (colSel_ + kCols - 1) % kCols;
        if (in.pressed(Btn::Right)) colSel_ = (colSel_ + 1) % kCols;
        if (in.pressed(Btn::A)) {
            if (colSel_ == kFaction) openPicker();  // 0.28.2 (owner): the faction picker
            else change(1);
        }
        if (in.pressed(Btn::Y)) change(-1);
        if (colSel_ == kFaction && in.pressed(Btn::R1)) change(5);
        if (colSel_ == kFaction && in.pressed(Btn::L1)) change(-5);
        if (in.pressed(Btn::X)) {  // back to random / automatic
            SeatSetup& s = seats_[size_t(seatSel_)];
            if (colSel_ == kFaction) s.faction = s.tribunii = -1;
            else if (colSel_ == kColour) s.color = -1;
            else if (colSel_ == kPers) s.arch = -1;
            else if (colSel_ == kLevel) s.level = 3;
        }
        if (in.pressed(Btn::B) || in.pressed(Btn::Start) || in.pressed(Btn::Menu)) menuOpen_ = false;
    }

    void update(App& app, const Input& in, uint32_t now) override {
        teOn_ = (app.settings().getInt("modules", ti::kModCodex) & ti::kModThundersEdge) != 0;
        modules_ = app.settings().getInt("modules", ti::kModCodex);
        dsOn_ = (app.settings().getInt("modules", ti::kModCodex) & ti::kModDiscordant) != 0;
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
            myColor_ = std::max(-1, std::min(7, app.settings().getInt("color", 0)));
            freeTrade_ = app.settings().getInt("trade_r1", 1) != 0;
            contested_ = app.settings().getInt("mecatol_contested", 1) != 0;  // (0.54.0)
            hyperlanes_ = app.settings().getInt("hyperlanes", 1) != 0;          // (0.58.0)
            draft_ = app.settings().getInt("galaxy_draft", 1) != 0;
            galaxy_ = std::max(0, std::min(2, app.settings().getInt("galaxy_mode", draft_ ? 1 : 0)));  // 0.43.0: + Seeded
            mapSeed_ = uint32_t(std::max(1, app.settings().getInt("map_seed", int(now % 99991u) + 1)));
            resetSeats();
            first_ = false;
        }
        if (pickerOpen_) {
            updatePicker(in);
            return;
        }
        if (menuOpen_) {
            updateMenu(in);
            return;
        }
        if (slicesOpen_) {  // the seeded galaxy's slices (0.43.0): left / right try the next seed, L1 / R1 jump
            if (in.pressed(Btn::Right)) setSeed(mapSeed_ + 1);
            if (in.pressed(Btn::Left)) setSeed(mapSeed_ > 1 ? mapSeed_ - 1 : 1);
            if (in.pressed(Btn::R1)) setSeed(mapSeed_ + 100);
            if (in.pressed(Btn::L1)) setSeed(mapSeed_ > 100 ? mapSeed_ - 100 : 1);
            if (in.pressed(Btn::X)) setSeed(1 + (now * 2654435761u >> 8) % 99991u);
            if (in.pressed(Btn::A) || in.pressed(Btn::B) || in.pressed(Btn::Y) || in.pressed(Btn::Menu)) slicesOpen_ = false;
            return;
        }
        if (sel_ == kGalaxy && galaxy_ == 2) {  // the seed: L1 / R1 change it, X a new one, Y its slices
            if (in.pressed(Btn::R1)) setSeed(mapSeed_ + 1);
            if (in.pressed(Btn::L1)) setSeed(mapSeed_ > 1 ? mapSeed_ - 1 : 1);
            if (in.pressed(Btn::X)) setSeed(1 + (now * 2654435761u >> 8) % 99991u);
            if (in.pressed(Btn::Y)) {
                slicesOpen_ = true;
                previewKey_ = 0;
                return;
            }
        }
        if (in.pressed(Btn::Up)) sel_ = (sel_ + kRows - 1) % kRows;
        if (in.pressed(Btn::Down)) sel_ = (sel_ + 1) % kRows;
        if (in.pressed(Btn::B) || in.pressed(Btn::Menu)) {
            app.pop();
            return;
        }
        int step = in.pressed(Btn::Right) ? 1 : in.pressed(Btn::Left) ? -1 : 0;
        if (in.pressed(Btn::A) && sel_ == kSeats) {
            menuOpen_ = true;
            seatSel_ = std::max(0, primary());
            colSel_ = kFaction;
            return;
        }
        if (in.pressed(Btn::A) && sel_ != kStart) step = 1;  // A also changes the selected row
        if (step) {
            switch (sel_) {
                case kPlayers:
                    players_ = std::max(3, std::min(8, players_ + (step > 0 ? 1 : -1)));
                    resetSeats();
                    break;
                case kVP: vp_ = vp_ == 10 ? 14 : 10; break;
                case kTrade: freeTrade_ = !freeTrade_; break;
                case kMecatol: contested_ = !contested_; break;
                case kHyperlanes: {  // (0.58.0) the seat at the bottom of the map can change: your seat moves with it
                    const int was = std::max(0, southernmostSeat(players_, layout()));
                    hyperlanes_ = !hyperlanes_;
                    const int now2 = std::max(0, southernmostSeat(players_, layout()));
                    if (was != now2) std::swap(seats_[size_t(was)], seats_[size_t(now2)]);
                    previewKey_ = 0;
                    break;
                }
                case kGalaxy: galaxy_ = (galaxy_ + (step > 0 ? 1 : 2)) % 3; break;  // Random, Draft, Seeded (0.43.0)
                case kSpeed: speed_ = (speed_ + (step > 0 ? 1 : 3)) % 4; break;
                case kSeats:
                    menuOpen_ = true;
                    seatSel_ = std::max(0, primary());
                    colSel_ = kFaction;
                    break;
                default: break;
            }
        }
        if ((in.pressed(Btn::A) && sel_ == kStart) || in.pressed(Btn::Start)) start(app, now);
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
        app.settings().save();
        Setup s;
        s.players = players_;
        s.human = watch ? std::max(0, southernmostSeat(players_, layout())) : me;
        s.vpGoal = vp_;
        s.humanColor = -1;
        s.trade = 1 | (freeTrade_ ? 2 : 0) | 4 | 8;  // community X-1 trades; round-1 free refills when a bot plays Trade (house rule)
        s.draft = galaxy_ == 1 ? 1 : 0;            // players build the galaxy (0.15.0) or a random one
        s.mapSeed = galaxy_ == 2 ? mapSeed_ : 0;   // ...or the map of the player's number (0.43.0)
        s.layout = layout();                        // the printed board without hyperlanes (0.58.0)
        s.modules = app.settings().getInt("modules", ti::kModCodex);  // the Modules screen (0.31.0)
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
        int levelCount[4] = {};
        for (int i = 0; i < players_; ++i) {
            const SeatSetup& st = seats_[size_t(i)];
            s.factions[size_t(i)] = st.faction;
            s.tribunii[size_t(i)] = st.tribunii;
            s.colors[size_t(i)] = st.color;
            if (st.human && i != s.human) s.humans |= 1 << i;
            if (!st.human || watch) {
                s.levels[size_t(i)] = st.level;
                s.archetypes[size_t(i)] = st.arch;
                ++levelCount[st.level];
            }
        }
        s.aiLevel = int(std::max_element(levelCount, levelCount + 4) - levelCount);  // the most common one
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
        int levels[4] = {};
        for (int i = 0; i < players_; ++i)
            if (!seats_[size_t(i)].human) ++levels[seats_[size_t(i)].level];
        std::string lv;
        int kinds = 0;
        for (int l = 0; l < 4; ++l)
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
    bool animating() const override { return pending_ != nullptr || retro::on() || retro::hd(); }
    // 0.44.1 (owner: the wheel "snapping to the factions instead of allowing me to free-scroll"): in the picker the
    // wheel scrolls the grid itself (updatePicker), not the chosen faction
    bool wheelScrolls(float x, float y) const override { return pickerOpen_ ? false : Scene::wheelScrolls(x, y); }  // the backdrops move (0.30.0, 0.33.1)
    void drawScreen(App&, Canvas& c, uint32_t now) {
        (void)now;
        if (retro::on()) backdrops::drawRetroNewGame(c, now);  // 0.30.0 (owner): the Retrowave New Game backdrop
        else if (!(retro::hd() && backdrops::drawVoidfallNewGame(c, now)))  // 0.33.1 (owner): Voidfall's own
            drawSpaceBackground(c, true);
        if (retro::hd()) {  // (0.33.1) the title and the line under the panel kept readable over the painting
            c.fillRoundRect(240, 18, 160, 30, 10, rgb(10, 4, 18), 170);
            c.fillRoundRect(40, 368, 560, 20, 8, rgb(10, 4, 18), 175);
        }
        drawTextAligned(c, FONT_LG, 0, 22, 640, Align::Center, "New game", theme::gold);
        Rect r{100, 44, 440, 318};  // (0.54.0: 7 rows - Mecatol Rex added; 0.58.0: 8 - Hyperlanes)
        drawPanel(c, r, theme::panel, theme::border, 12, retro::on() || retro::hd() ? 228 : 255);
        struct R {
            const char* label;
            std::string value, detail;
        } rows[kRows - 1] = {
            {"Players", std::to_string(players_), ""},
            {"Factions & seats", "Set up", seatsSummary()},
            {"Victory points", std::to_string(vp_), ""},
            {"Round 1 trade", freeTrade_ ? "Free refills" : "As printed",
             freeTrade_ ? "A computer playing Trade refills everyone" : "Trade card as printed in round 1"},
            {"Galaxy", galaxy_ == 1 ? "Draft" : galaxy_ == 2 ? "Seed " + std::to_string(mapSeed_) : "Random",
             galaxy_ == 1 ? "Everyone places tiles in turn"
             : galaxy_ == 2 ? keyLabel("L1") + "/" + keyLabel("R1") + " seed, " + keyLabel("X") + " new, " + keyLabel("Y") + " the slices"
                            : "A balanced random map"},
            {"Hyperlanes", hyperlanes_ ? "On" : "Off", hyperlaneDetail()},
            {"Mecatol Rex", contested_ ? "Contested" : "Calm",
             contested_ ? "Bots fight its holder for it - Imperial's points" : "Bots take it only when its holder leaves it open"},
            {"AI speed", kSpeedNames[speed_], ""},
        };
        const int rowH = 31;
        int y = r.y + 14;
        for (int i = 0; i < kRows - 1; ++i) {
            const bool sel = i == sel_;
            if (!menuOpen_) {  // the mouse (0.27.0): point at a row, click its arrows to change it
                hits::add(Rect{r.x + 12, y - 3, r.w - 24, rowH - 4}, [this, i]() { sel_ = i; });
                if (sel && i != kSeats) {
                    hits::addButton(Rect{r.x + r.w - 228, y - 3, 26, rowH - 4}, Btn::Left);
                    hits::addButton(Rect{r.x + r.w - 46, y - 3, 30, rowH - 4}, Btn::Right);
                }
            }
            if (sel) c.fillRoundRect(r.x + 12, y - 3, r.w - 24, rowH - 4, 6, theme::panelHi);
            drawText(c, FONT_MDB, r.x + 26, y + (rows[i].detail.empty() ? 6 : 1), rows[i].label, sel ? theme::text : theme::dim);
            const int right = r.x + r.w - 46;
            drawTextAligned(c, FONT_MDB, r.x, y + (rows[i].detail.empty() ? 6 : 1), right - r.x, Align::Right, rows[i].value, theme::gold);
            if (!rows[i].detail.empty())
                drawTextAligned(c, FONT_XS, r.x, y + 19, right - r.x, Align::Right, fitText(FONT_XS, rows[i].detail, 300), theme::faint);
            if (sel && i != kSeats) {
                drawText(c, FONT_SM, r.x + r.w - 220, y + 8, "\xE2\x97\x80", theme::dim);
                drawText(c, FONT_SM, r.x + r.w - 38, y + 8, "\xE2\x96\xB6", theme::dim);
            }
            y += rowH;
        }
        const bool sel = sel_ == kStart;
        const bool watch = primary() < 0;
        Rect b{r.x + (r.w - 180) / 2, y + 12, 180, 40};
        if (!menuOpen_) hits::add(b, [this]() { sel_ = kStart; });
        c.fillRoundRect(b.x, b.y, b.w, b.h, 10, sel ? theme::gold : theme::panelHi);
        drawTextAligned(c, FONT_LG, b.x, b.y + 8, b.w, Align::Center, watch ? "Watch game" : "Start game", sel ? theme::black : theme::text);
        drawTextAligned(c, FONT_XS, 0, 372, 640, Align::Center,
                        keyLabel("A") + " on \"Factions & seats\": who plays each seat, factions, colours, difficulty, personalities.",
                        theme::faint);
        if (!error_.empty()) drawTextAligned(c, FONT_SM, 0, 390, 640, Align::Center, error_, theme::red);
        if (slicesOpen_) {
            drawSlices(c, now);
            return;
        }
        if (menuOpen_) {
            drawMenu(c);
            if (pickerOpen_) {
                hits::clear();  // 0.28.4: the seat table under the picker must not take the mouse (it changed another seat)
                drawPicker(c, now);
            }
            return;
        }
        drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"A", "Choose"}, {"START", watch ? "Watch" : "Begin"}, {"B", "Back"}});
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

    // ------------------------------------------------------------ the seeded galaxy (0.43.0)
    void setSeed(uint32_t v) {
        mapSeed_ = std::max<uint32_t>(1, std::min<uint32_t>(999999u, v));
        previewKey_ = 0;
    }
    // The map this seed makes for these seats, built the way the game will build it, and each slice's numbers.
    void buildPreview() {
        const uint64_t key = (uint64_t(mapSeed_) << 8) ^ uint64_t(players_) ^ (uint64_t(modules_) << 40) ^ (uint64_t(layout()) << 60);
        if (key == previewKey_) return;
        previewKey_ = key;
        preview_ = ti::Game();
        preview_.numPlayers = players_;
        preview_.modules = modules_;
        preview_.layout = uint8_t(layout());
        preview_.players.assign(size_t(players_), ti::Player());
        std::vector<int> homes(static_cast<size_t>(players_), 0);
        for (int i = 0; i < players_; ++i) {
            const int f = seats_[size_t(i)].faction >= 0 ? seats_[size_t(i)].faction : i % ti::kFactionCount;  // (random seats: a stand-in home)
            homes[size_t(i)] = f;
            preview_.players[size_t(i)].faction = int8_t(f);
            preview_.players[size_t(i)].color = int8_t(seats_[size_t(i)].color >= 0 ? seats_[size_t(i)].color : i);
        }
        ti::Rng mr(uint32_t(mapSeed_ * 2654435761u + uint32_t(players_)));  // (exactly as the game seeds it: 32-bit)
        std::string err;
        owner_.clear();
        previewOk_ = ti::buildGalaxy(preview_, mr, homes, &err, true, &owner_);
        if (previewOk_) {
            ti::computeAdjacency(preview_);
            stats_ = ti::sliceStats(preview_, owner_, players_);
        }
    }
    void drawSlices(Canvas& c, uint32_t now) {
        buildPreview();
        c.fillRect(0, 0, 640, 480, rgb(6, 8, 16), 235);
        drawText(c, FONT_LG, 14, 8, "Seed " + std::to_string(mapSeed_), theme::gold);
        drawTextAligned(c, FONT_XS, 0, 14, 626, Align::Right, std::to_string(players_) + " players \xC2\xB7 each slice balanced", theme::dim);
        if (!previewOk_) {
            drawTextAligned(c, FONT_MD, 0, 200, 640, Align::Center, "No legal map for this seed - try another.", theme::red);
        } else {
            MapView map;
            map.setViewport(Rect{8, 38, 340, 330});
            map.setInfoTiles(false);
            map.setZoom(0);
            map.setCursor(preview_, std::max(0, preview_.mecatol()));
            map.draw(c, preview_, now);
            // the slices: a coloured dot on each slice tile
            // (the table on the right: what each seat's five tiles hold)
            const int tx = 356, tw = 276;
            int y = 44;
            const char* heads[] = {"Res", "Inf", "Best", "Pl", "Leg", "Sk", "Wh", "An"};
            for (int k = 0; k < 8; ++k) drawTextAligned(c, FONT_XS, tx + 100 + k * 22, y, 22, Align::Center, heads[k], theme::dim);
            y += 14;
            for (int i = 0; i < players_ && i < int(stats_.size()); ++i) {
                const SeatSetup& st = seats_[size_t(i)];
                const uint32_t col = theme::player[preview_.players[size_t(i)].color % 8];
                c.fillRect(tx, y + 2, 4, 26, col);
                const std::string who = st.faction >= 0 ? ti::factionShortName(st.faction) : std::string("Random");
                drawText(c, FONT_SMB, tx + 8, y, fitText(FONT_SMB, "Seat " + std::to_string(i + 1), 90), col);
                drawText(c, FONT_XS, tx + 8, y + 15, fitText(FONT_XS, who, 90), theme::faint);
                const ti::SliceStats& ss = stats_[size_t(i)];
                const int v[8] = {ss.res, ss.inf, ss.optimal, ss.planets, ss.legendary, ss.skips, ss.wormholes, ss.anomalies};
                for (int k = 0; k < 8; ++k)
                    drawTextAligned(c, FONT_SMB, tx + 100 + k * 22, y + 7, 22, Align::Center, std::to_string(v[k]), k == 2 ? theme::gold : theme::text);
                y += 32;
            }
            drawWrapped(c, FONT_XS, tx, y + 6, tw,
                        "Res / Inf: resources and influence of the slice's planets. Best: each planet at its better value. Pl planets, "
                        "Leg legendary, Sk technology specialties, Wh wormholes, An anomalies. The same seed and number of players "
                        "always make this map.",
                        theme::faint);
        }
        drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Seed -/+1"}, {"L1/R1", "-/+100"}, {"X", "New seed"}, {"B", "Back"}});
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
    };
    std::vector<PickEntry> picks_;
    std::vector<int> pickX_, pickY_;  // layout, in content units (y grows down; headers take space)
    int pickH_ = 0;
    bool pickerOpen_ = false;
    bool teOn_ = false;  // Thunder's Edge ticked on the Modules screen
    bool dsOn_ = false;  // Discordant Stars too
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
    bool pickable(const PickEntry& e) const { return !e.soonKey && (e.faction < 0 || factionFree(e.faction, pickSeat_, e.tribunii)); }
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
                pickMsg_ = e.group == 2 ? (dsOn_ ? "This Discordant Stars faction is coming soon."
                                                 : "Tick Discordant Stars on the Modules screen (title screen) to play its factions.")
                           : teOn_      ? "This Thunder's Edge faction is coming soon."
                                        : "Tick Thunder's Edge on the Modules screen (title screen) to play its factions.";
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
        const char* kGroup[3] = {"Prophecy of Kings and the Council Keleres",
                                 teOn_ ? "Thunder's Edge" : "Thunder's Edge - tick it on the Modules screen",
                                 dsOn_ ? "Discordant Stars (fan-made)" : "Discordant Stars (fan-made) - tick it on the Modules screen"};
        int lastGroup = -1;
        for (size_t i = 0; i < picks_.size(); ++i) {
            const PickEntry& e = picks_[i];
            const int x = area.x + pickX_[i], y = area.y + pickY_[i] - pickScroll_;
            if (e.group != lastGroup) {
                lastGroup = e.group;
                const int hy = y - kHeadH + 2;
                if (hy > area.y - kHeadH && hy < area.y + area.h)
                    drawText(c, FONT_SMB, area.x, hy, kGroup[e.group],
                             e.group == 0 || (e.group == 1 && teOn_) || (e.group == 2 && dsOn_) ? theme::gold : rgb(150, 150, 170));
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
            else drawTextAligned(c, FONT_LG, cr.x, iy + 6, cr.w, Align::Center, "?", theme::dim);
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
        if (pickScroll_ > 0) drawTextAligned(c, FONT_XS, r.x, area.y - 12, r.w - 20, Align::Right, "\xE2\x96\xB2 more", theme::dim);
        if (pickScroll_ + area.h < pickH_)
            drawTextAligned(c, FONT_XS, r.x, area.y + area.h + 1, r.w - 20, Align::Right, "\xE2\x96\xBC more", theme::dim);
        // the chosen one, with a line about it
        const PickEntry& e = picks_[size_t(pickSel_)];
        const int dy = area.y + area.h + 14;
        c.fillRoundRect(r.x + 12, dy, r.w - 24, 70, 8, rgb(18, 24, 40));
        if (e.soonKey) icons::draw(c, std::string("faction/") + e.soonKey, r.x + 20, dy + 7, 56, 170);
        else if (e.faction >= 0) drawFactionIcon(c, e.faction, r.x + 20, dy + 7, 56);
        const int tx = r.x + 86;
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
            const bool rowSel = i == seatSel_;
            if (rowSel) c.fillRoundRect(r.x + 6, y, r.w - 12, rowH - 3, 6, theme::panelHi);
            const int ty = y + (rowH - 3) / 2 - FONT_SMB.lineHeight / 2;
            drawText(c, FONT_SMB, r.x + 12, ty - (i == bottom ? 6 : 0), std::to_string(i + 1), theme::dim);
            if (i == bottom) drawText(c, FONT_XS, r.x + 8, ty + 9, "bottom", theme::faint);
            for (int col = 0; col < kCols; ++col)  // the mouse (0.27.0): point at a cell, click to change it
                hits::add(Rect{kX[col], y, kX[col + 1] - kX[col] - 4, rowH - 3}, [this, i, col]() {
                    seatSel_ = i;
                    colSel_ = col;
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
                    c.fillCircle(float(x + 16), float(y + (rowH - 3) / 2), 8.f, theme::player[s.color]);
                    drawText(c, FONT_SM, x + 28, ty + 1, theme::playerColorName[s.color], theme::text);
                } else {
                    drawText(c, FONT_SMB, x + 6, ty, "Auto", theme::dim);
                }
            }
            if (s.human) {
                cell(kLevel, "-", theme::faint);
                cell(kPers, "-", theme::faint);
            } else {
                cell(kLevel, kLevels[s.level], s.level == 3 ? theme::gold : theme::text);
                cell(kPers, s.arch < 0 ? "Random" : kArchetypes[s.arch].name, s.arch < 0 ? theme::dim : theme::text);
            }
            y += rowH;
        }
        // what the selected cell means
        const SeatSetup& s = seats_[size_t(seatSel_)];
        std::string info;
        switch (Col(colSel_)) {
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
            case kColour: info = s.color < 0 ? "Auto: the next colour nobody picked." : "Each colour can be used by one seat only."; break;
            case kLevel: info = s.human ? "Difficulty is for computer players." : kLevelInfo[s.level]; break;
            case kPers:
                info = s.human ? "Personalities are for computer players."
                               : s.arch < 0 ? (s.level == 3 ? "Random: any archetype but Rusher and Chaotic (Competitive bots). Hidden until the end of round 3."
                                                            : "Random: any of the ten archetypes, Chaotic included. Hidden until the end of round 3.")
                                            : std::string(kArchetypes[s.arch].blurb) + ".";
                break;
            default: break;
        }
        auto lines = wrapText(FONT_SM, info, r.w - 40);
        int yy = r.y + r.h - 12 - int(std::min<size_t>(2, lines.size())) * 16;
        for (size_t k = 0; k < lines.size() && k < 2; ++k) {
            drawTextAligned(c, FONT_SM, r.x, yy, r.w, Align::Center, lines[k], theme::text);
            yy += 16;
        }
        drawHintBar(c, {{"D-PAD", "Move"}, {"A", "Next"}, {"Y", "Previous"}, {"X", "Random"}, {"B", "Done"}});
    }

private:
    std::vector<std::pair<int, int>> factions_;  // (faction, Keleres home: -1 = random)
    bool freeTrade_ = true, draft_ = true;
    bool contested_ = true;  // Mecatol Rex: Contested / Calm (0.54.0, Setup::extras bit 128)
    bool hyperlanes_ = true; // Hyperlanes: On / Off (0.58.0, Setup::layout 0 / 1)
    int galaxy_ = 1;              // 0 Random, 1 Draft, 2 Seeded (0.43.0)
    uint32_t mapSeed_ = 1;
    bool slicesOpen_ = false, previewOk_ = false;
    uint64_t previewKey_ = 0;
    ti::Game preview_;
    std::vector<int> owner_;
    std::vector<ti::SliceStats> stats_;
    int modules_ = 1;
    int sel_ = kStart, players_ = 6, vp_ = 10, speed_ = 1, myColor_ = 0;
    bool first_ = true;
    bool menuOpen_ = false;
    int seatSel_ = 0, colSel_ = kFaction;
    std::array<SeatSetup, 8> seats_{};
    std::string error_;
};

}  // namespace

std::unique_ptr<Scene> makeNewGameScene() { return std::unique_ptr<Scene>(new NewGameScene()); }
