#include "ui/decision_ui.h"
#include "ui/hits.h"

#include "ui/cards.h"

#include <algorithm>
#include <deque>
#include <map>
#include <cstring>
#include <set>

#include "ai/ai.h"
#include "engine/query.h"
#include "engine/rules.h"
#include "gfx/font.h"
#include "ui/deal_ui.h"
#include "ui/decor_tiles.h"
#include "ui/draft_ui.h"
#include "ui/icons.h"
#include "ui/info_screens.h"
#include "ui/theme.h"
#include "ui/tile_view.h"
#include "ui/widgets.h"

using namespace ti;

Rect decisionPanelRect() { return Rect{6, 26, 410, 362}; }  // leaves room for the event feed below

namespace {

// Frame with title, context and error; returns the y where content starts.
// contentH > 0 shrinks the panel to fit that much content (short lists).
int drawFrame(Canvas& c, const std::string& title, const std::string& context, int* bottom, int contentH = -1) {
    Rect r = decisionPanelRect();
    const std::vector<std::string> titleLines = wrapText(FONT_MDB, title, r.w - 24);
    if (contentH > 0) {
        int ctxLines = context.empty() ? 0 : std::min<int>(4, int(wrapText(FONT_SM, context, r.w - 24).size()));
        int need = 8 + int(titleLines.size()) * 19 + ctxLines * 15 + 10 + contentH + 10;
        r.h = std::min(r.h, std::max(need, 90));
    }
    drawPanel(c, r, rgb(12, 17, 32), theme::gold, 10);
    int y = r.y + 8;
    for (const std::string& line : titleLines) {
        drawText(c, FONT_MDB, r.x + 12, y, line, theme::gold);
        y += 19;
    }
    if (!context.empty()) {
        // 0.43.0: the text after kRedNote (a rule reminder, e.g. "Mecatol Rex can't be used") is drawn in red
        const size_t cut = context.find(kRedNote);
        const std::string dim = context.substr(0, cut), red = cut == std::string::npos ? "" : context.substr(cut + 1);
        int n = 0;
        for (const std::string& line : wrapText(FONT_SM, dim, r.w - 24)) {
            if (n++ >= 3) break;
            drawText(c, FONT_SM, r.x + 12, y, line, theme::dim);
            y += 15;
        }
        for (const std::string& line : red.empty() ? std::vector<std::string>() : wrapText(FONT_SM, red, r.w - 24)) {
            if (n++ >= 4) break;
            drawText(c, FONT_SM, r.x + 12, y, line, theme::red);
            y += 15;
        }
    }
    y += 4;
    c.hline(r.x + 10, r.x + r.w - 10, y, theme::border);
    if (bottom) *bottom = r.y + r.h - 8;
    return y + 6;
}

// warning = true: a live hint about the current choice (amber) rather than an error (red)
void drawError(Canvas& c, const std::string& err, int y, bool warning = false) {
    if (err.empty()) return;
    Rect r = decisionPanelRect();
    auto lines = wrapText(FONT_SM, err, r.w - 24);
    int h = int(lines.size()) * 15 + 6;
    c.fillRoundRect(r.x + 8, y - h, r.w - 16, h, 6, warning ? rgb(60, 44, 12) : rgb(70, 18, 18), 235);
    int yy = y - h + 3;
    for (const std::string& l : lines) {
        drawText(c, FONT_SM, r.x + 14, yy, l, warning ? rgb(255, 225, 150) : rgb(255, 200, 200));
        yy += 15;
    }
}

// =================================================================== list picks
class ListUI : public DecisionUI {
    bool confirmPass_ = false;  // 0.30.0: asking "are you sure" about Pass

public:
    bool bareClickConfirms() const override { return false; }  // (0.58.4)
    ListUI(const Game& g, const Decision& d) : DecisionUI(g, d) {
        multi_ = d.maxPick > 1 || d.minPick != 1;
        for (size_t i = 0; i < d_.options.size(); ++i)
            if (d_.options[i].enabled) {
                cursor_ = int(i);
                break;
            }
    }

    int highlightedValue() const override {
        return cursor_ >= 0 && cursor_ < int(d_.options.size()) ? d_.options[cursor_].id : -1;
    }
    std::vector<std::pair<std::string, std::string>> hints() const override {
        if (confirmPass_) return {{"A", "Pass"}, {"B", "Keep playing"}};
        if (multi_) {
            std::vector<std::pair<std::string, std::string>> h = {{"A", "Toggle"}, {"START", "Confirm"}};
            if (!chosen_.empty()) h.push_back({"B", "Clear"});
            return h;
        }
        // 0.43.4 (owner + tester): B never answers ("No", "Skip", "Nothing" are picked with A) - it is back / undo
        return {{"A", "Choose"}};
    }
    bool backAvailable() const override { return multi_ ? !chosen_.empty() : confirmPass_; }

    bool update(const Input& in, uint32_t) override {
        const int n = int(d_.options.size());
        if (n == 0) return false;
        if (in.pressed(Btn::Up)) cursor_ = (cursor_ + n - 1) % n;
        if (in.pressed(Btn::Down)) cursor_ = (cursor_ + 1) % n;
        if (in.pressed(Btn::L1)) cursor_ = std::max(0, cursor_ - 5);
        if (in.pressed(Btn::R1)) cursor_ = std::min(n - 1, cursor_ + 5);
        if (!multi_) {
            if (confirmPass_) {  // 0.30.0 (owner): "are you sure" before passing for the round
                if (in.pressed(Btn::A)) {
                    answer_ = Answer();
                    answer_.picks.push_back(99);
                    return true;
                }
                if (in.pressed(Btn::B)) confirmPass_ = false;
                return false;
            }
            if (in.pressed(Btn::A)) {
                if (!d_.options[cursor_].enabled) {
                    setError("That option isn't available.");
                    return false;
                }
                if (d_.tag == Tag::ActionChoice && d_.options[cursor_].id == 99 && n > 1) {
                    confirmPass_ = true;
                    return false;
                }
                answer_ = Answer();
                answer_.picks.push_back(d_.options[cursor_].id);
                return true;
            }
            return false;  // (B: the scene goes back a step - 0.43.4)
        }
        if (in.pressed(Btn::A)) {
            int id = d_.options[cursor_].id;
            if (chosen_.count(id)) chosen_.erase(id);
            else if (int(chosen_.size()) < d_.maxPick) chosen_.insert(id);
            else setError("You can choose at most " + std::to_string(d_.maxPick) + ".");
        }
        if (in.pressed(Btn::B)) chosen_.clear();
        if (in.pressed(Btn::Start)) {
            answer_ = Answer();
            for (const Option& o : d_.options)
                if (chosen_.count(o.id)) answer_.picks.push_back(o.id);
            return true;
        }
        return false;
    }

    void draw(Canvas& c, uint32_t) override {
        int bottom;
        std::string ctx = d_.context;
        if (multi_)
            ctx += (ctx.empty() ? "" : " ") + std::string("Choose ") +
                   (d_.minPick == d_.maxPick ? std::to_string(d_.minPick)
                                             : std::to_string(d_.minPick) + "-" + std::to_string(d_.maxPick)) +
                   keyText(", then press START.");
        Rect r = decisionPanelRect();
        const int w = r.w - 24;
        // compute row heights
        std::vector<std::vector<std::string>> details;
        std::vector<int> heights;
        int total = 0;
        for (const Option& o : d_.options) {
            auto lines = wrapText(FONT_XS, o.detail, w - 30);
            if (lines.size() > 2) {
                lines.resize(2);
                lines[1] = fitText(FONT_XS, lines[1] + " ...", w - 30);
            }
            details.push_back(lines);
            heights.push_back(21 + int(lines.size()) * 13 + 4);
            total += heights.back();
        }
        int y0 = drawFrame(c, d_.prompt, ctx, &bottom, total + (error_.empty() ? 0 : 34));
        // scroll so the cursor is visible
        int avail = bottom - y0 - (error_.empty() ? 0 : 30);
        if (cursor_ < scroll_) scroll_ = cursor_;
        for (;;) {
            int h = 0;
            for (int i = scroll_; i <= cursor_ && i < int(heights.size()); ++i) h += heights[i];
            if (h <= avail || scroll_ >= cursor_) break;
            ++scroll_;
        }
        int y = y0;
        for (int i = scroll_; i < int(d_.options.size()); ++i) {
            if (y + heights[i] > y0 + avail) break;
            const Option& o = d_.options[i];
            bool sel = i == cursor_;
            hits::add(Rect{r.x + 8, y - 2, r.w - 16, heights[i]}, [this, i]() { cursor_ = i; });  // the mouse (0.27.0)
            if (sel) {
                c.fillRoundRect(r.x + 8, y - 2, r.w - 16, heights[i], 6, theme::panelHi, 245);
                c.frameRoundRect(r.x + 8, y - 2, r.w - 16, heights[i], 6, theme::gold, 1);
            }
            int x = r.x + 16;
            if (multi_) {
                bool on = chosen_.count(o.id) > 0;
                c.frameRect(x, y + 3, 12, 12, theme::text, 1);
                if (on) c.fillRect(x + 3, y + 6, 6, 6, theme::gold);
                x += 18;
            }
            uint32_t col = !o.enabled ? theme::faint : sel ? theme::text : rgb(190, 196, 212);
            drawText(c, FONT_MDB, x, y, fitText(FONT_MDB, o.label, r.x + r.w - 16 - x), col);
            int yy = y + 20;
            for (const std::string& l : details[i]) {
                drawText(c, FONT_XS, x, yy, l, sel ? theme::dim : theme::faint);
                yy += 13;
            }
            y += heights[i];
        }
        if (scroll_ > 0) drawText(c, FONT_SM, r.x + r.w - 24, y0 - 4, "\xE2\x96\xB2", theme::dim);
        drawError(c, error_, bottom);
        if (confirmPass_) {
            const Rect b{r.x + 20, r.y + r.h / 2 - 40, r.w - 40, 80};
            c.fillRoundRect(b.x, b.y, b.w, b.h, 10, rgb(14, 18, 32), 245);
            c.frameRoundRect(b.x, b.y, b.w, b.h, 10, theme::gold, 2);
            drawTextAligned(c, FONT_MDB, b.x, b.y + 12, b.w, Align::Center, "Pass for the rest of the round?", theme::gold);
            drawTextAligned(c, FONT_SM, b.x, b.y + 38, b.w, Align::Center, "You take no more actions until the next round.", theme::dim);
            hits::addButton(Rect{b.x, b.y, b.w / 2, b.h}, Btn::A);
            hits::addButton(Rect{b.x + b.w / 2, b.y, b.w / 2, b.h}, Btn::B);
            drawTextAligned(c, FONT_SMB, b.x, b.y + 58, b.w, Align::Center, keyText("A: Pass     B: Keep playing"), theme::text);
        }
    }

private:
    bool multi_ = false;
    int cursor_ = 0, scroll_ = 0;
    std::set<int> chosen_;
};

// =================================================================== map picks
class MapPickUI : public DecisionUI {
public:
    MapPickUI(const Game& g, const Decision& d, MapView& map) : DecisionUI(g, d), map_(map), list_(g, d) {
        std::vector<int> sys;
        for (const Option& o : d_.options)
            if (o.id >= 0 && o.enabled && o.id < int(g.systems.size())) sys.push_back(o.id);
        map_.setHighlights(sys);
        if (!sys.empty() && !map_.highlighted(map_.cursor())) {
            // start on the nearest highlighted system (from an info / placeholder tile: from the first one)
            int best = sys[0], bd = 1 << 20;
            const int from = map_.cursor() >= 0 && map_.cursor() < int(g.systems.size()) ? map_.cursor() : sys[0];
            for (int s : sys) {
                int dd = hexDistance(g.systems[s].pos, g.systems[from].pos);
                if (dd < bd) {
                    bd = dd;
                    best = s;
                }
            }
            map_.setCursor(g, best);
        }
    }
    ~MapPickUI() override { map_.clearHighlights(); }

    bool mapMode() const override { return !listMode_; }
    bool bareClickConfirms() const override { return mapMode(); }  // (0.58.4: tiles on the map, not the list)

    std::vector<std::pair<std::string, std::string>> hints() const override {
        if (listMode_) return {{"A", "Choose"}, {"Y", "Map"}, {"B", "Map"}};
        std::vector<std::pair<std::string, std::string>> h = {{"D-PAD", "Move"}, {"X", "Next"}, {"A", "Choose"}, {"Y", "List"}};
        for (const Option& o : d_.options)  // 0.43.4: "Done" / "Skip" on START - B is back / undo only
            if (o.id < 0 && o.enabled) {
                h.push_back({"START", o.label});
                break;
            }
        return h;
    }
    bool backAvailable() const override { return listMode_; }

    bool update(const Input& in, uint32_t now) override {
        if (in.pressed(Btn::Y)) {
            listMode_ = !listMode_;
            return false;
        }
        if (listMode_) {
            if (in.pressed(Btn::B)) {  // back to the map
                listMode_ = false;
                return false;
            }
            if (list_.update(in, now)) {
                answer_ = list_.answer();
                return true;
            }
            return false;
        }
        if (in.pressed(Btn::Up)) map_.moveCursor(g_, 0);
        if (in.pressed(Btn::Right)) map_.moveCursor(g_, 1);
        if (in.pressed(Btn::Down)) map_.moveCursor(g_, 2);
        if (in.pressed(Btn::Left)) map_.moveCursor(g_, 3);
        if (int z = in.zoom()) map_.setZoom(map_.zoom() + z);  // L1 / R1, mouse wheel
        if (in.pressed(Btn::X)) {  // jump to the next system that can be chosen
            std::vector<int> sys;
            for (const Option& o : d_.options)
                if (o.id >= 0 && o.enabled) sys.push_back(o.id);
            if (!sys.empty()) {
                auto it = std::find(sys.begin(), sys.end(), map_.cursor());
                int next = (it == sys.end()) ? sys[0] : sys[(it - sys.begin() + 1) % sys.size()];
                map_.setCursor(g_, next);
            }
            error_.clear();
        }
        if (in.pressed(Btn::A)) {
            if (!map_.highlighted(map_.cursor())) {
                setError("You can't choose that system here.");
                return false;
            }
            answer_ = Answer();
            answer_.picks.push_back(map_.cursor());
            return true;
        }
        if (in.pressed(Btn::Start)) {
            for (const Option& o : d_.options)
                if (o.id < 0 && o.enabled) {
                    answer_ = Answer();
                    answer_.picks.push_back(o.id);
                    return true;
                }
        }
        return false;
    }

    void draw(Canvas& c, uint32_t now) override {
        if (listMode_) {
            list_.draw(c, now);
            return;
        }
        Rect r{6, 26, 410, 0};
        auto lines = wrapText(FONT_MDB, d_.prompt, r.w - 20);
        int h = int(lines.size()) * 19 + 10 + (d_.context.empty() ? 0 : 15);
        c.fillRoundRect(r.x, r.y, r.w, h, 8, rgb(12, 17, 32), 225);
        c.frameRoundRect(r.x, r.y, r.w, h, 8, theme::cyan, 1);
        int y = r.y + 5;
        for (const std::string& l : lines) {
            drawText(c, FONT_MDB, r.x + 10, y, l, theme::cyan);
            y += 19;
        }
        if (!d_.context.empty()) {
            const size_t cut = d_.context.find(kRedNote);  // a red rule reminder takes the line when there is one
            if (cut == std::string::npos) drawText(c, FONT_SM, r.x + 10, y, fitText(FONT_SM, d_.context, r.w - 20), theme::dim);
            else drawText(c, FONT_SM, r.x + 10, y, fitText(FONT_SM, d_.context.substr(cut + 1), r.w - 20), theme::red);
        }
        if (!error_.empty()) {
            int ey = 446 - 70;
            c.fillRoundRect(r.x, ey, r.w, 22, 6, rgb(70, 18, 18), 235);
            drawText(c, FONT_SM, r.x + 8, ey + 3, fitText(FONT_SM, error_, r.w - 16), rgb(255, 200, 200));
        }
    }

private:
    MapView& map_;
    ListUI list_;
    bool listMode_ = false;
};

// =================================================================== editable rows
struct Row {
    std::string label, sub;
    int value = 0, min = 0, max = 0;
    bool header = false;
    bool info = false;                 // (with header) a dim line of information - 0.44.2: ships that can't reach, and why
    std::vector<std::string> choices;  // cycling text choices (value = index)
    int a = 0, b = 0, c = 0;           // encoding helpers
    const char* icon = nullptr;        // drawn in front of the label
    int token = 0;                     // a command token in front of the label: 1 faction side, 2 ship side
    int res = -1, inf = -1;            // planet values, shown as icons instead of `sub`
};

class RowsUI : public DecisionUI {
public:
    bool bareClickConfirms() const override { return mapMode(); }  // (0.58.4: Waypoint Mode picks stops on the map)
    using DecisionUI::DecisionUI;

    // 0.28.0 (owner, keyboard): moving ships - Space moves, Q / E fewer / more
    virtual bool spaceConfirms() const { return true; }  // 0.29.0 (owner): every editor, not only moving
    bool stepsWithKeys() const override { return true; }
    bool kbConfirm() const { return spaceConfirms() && keyboardKeys(); }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        std::vector<std::pair<std::string, std::string>> h = {{"\xE2\x97\x80\xE2\x96\xB6", "Change"},
                                                                {"START", confirmLabel()}};
        if (kbConfirm()) h = {{"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"L1/R1", "Less/more"}, {"A", confirmLabel()}};
        if (hasSuggestion()) h.push_back({"Y", "Suggest"});
        if (onBackAvailable()) h.push_back({"X", declineLabel()});  // 0.43.4: declining is X, B is back / undo
        if (backAvailable()) h.push_back({"B", clearLabel()});
        return h;
    }
    bool backAvailable() const override {
        for (const Row& x : rows_)
            if (!x.header && x.choices.empty() && x.value != x.min) return true;
        return false;
    }

    bool update(const Input& in, uint32_t) override {
        if (!warn_.empty()) {  // 0.43.0: "this costs you something - go on?"
            if (in.pressed(Btn::A) || in.pressed(Btn::Start)) {
                warn_.clear();
                return true;
            }
            if (in.pressed(Btn::B)) warn_.clear();
            return false;
        }
        if (rows_.empty()) {
            if (in.pressed(Btn::Start) || in.pressed(Btn::A)) {
                buildAnswer();
                return true;
            }
            return false;
        }
        if (in.pressed(Btn::Up)) moveCursor(-1);
        if (in.pressed(Btn::Down)) moveCursor(1);
        Row& r = rows_[cursor_];
        if (!r.header) {
            int step = 0;
            const bool kb = kbConfirm();
            if (in.pressed(Btn::Right) || (!kb && in.pressed(Btn::A))) step = 1;
            if (in.pressed(Btn::Left)) step = -1;
            if (in.pressed(Btn::R1)) step = kb ? 1 : 5;
            if (in.pressed(Btn::L1)) step = kb ? -1 : -5;
            if (step) stepRow(cursor_, step, !kb && in.pressed(Btn::A));
        }
        if (in.pressed(Btn::Y) && hasSuggestion()) {
            suggest();
            changed();
        }
        if (in.pressed(Btn::X) && onBackAvailable() && onBack()) return true;  // 0.43.4: decline (optional payment)
        if (in.pressed(Btn::B)) {
            for (Row& x : rows_)
                if (!x.header && x.choices.empty()) x.value = x.min;
            changed();
        }
        if (in.pressed(Btn::Start) || (kbConfirm() && in.pressed(Btn::A))) {
            buildAnswer();
            std::string e = d_.validate ? d_.validate(answer_) : "";
            if (e.empty()) {
                warn_ = confirmWarning();
                if (warn_.empty()) return true;
                return false;
            }
            setError(e);
        }
        return false;
    }

    void draw(Canvas& c, uint32_t) override {
        int bottom;
        int y0 = drawFrame(c, d_.prompt, d_.context, &bottom);
        Rect r = decisionPanelRect();
        std::vector<std::string> sum = summary();
        int sumH = int(sum.size()) * 15 + (sum.empty() ? 0 : 8);
        int errH = live_.empty() && error_.empty() ? 0 : 34;
        int avail = bottom - y0 - sumH - errH;
        const int rowH = 22;
        int visible = std::max(1, avail / rowH);
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        int y = y0;
        if (rows_.empty()) drawText(c, FONT_SM, r.x + 16, y, emptyText(), theme::dim);
        for (int i = scroll_; i < int(rows_.size()) && i < scroll_ + visible; ++i) {
            const Row& row = rows_[i];
            if (row.header && row.info) {
                drawText(c, FONT_SM, r.x + 20, y + 3, fitText(FONT_SM, row.label, 190), theme::faint);
                drawText(c, FONT_XS, r.x + 214, y + 4, fitText(FONT_XS, row.sub, r.w - 230), theme::faint);
                y += rowH;
                continue;
            }
            if (row.header) {
                drawText(c, FONT_SMB, r.x + 14, y + 4, fitText(FONT_SMB, row.label, r.w - 30), row.label.rfind("Can't", 0) == 0 ? theme::dim : theme::cyan);
                y += rowH;
                continue;
            }
            bool sel = i == cursor_;
            if (sel) c.fillRoundRect(r.x + 8, y, r.w - 16, rowH - 2, 5, theme::panelHi, 245);
            {  // the mouse (0.27.0): point at a row; click its arrows to change it
                if (kbConfirm())  // a click picks the row; on the picked row it adds one (toggles a planet)
                    hits::addAction(Rect{r.x + 8, y, r.w - 16, rowH - 2}, [this, i]() {
                        if (cursor_ == i) stepRow(i, 1, true);
                        else cursor_ = i;
                    });
                else hits::add(Rect{r.x + 8, y, r.w - 16, rowH - 2}, [this, i]() { cursor_ = i; });
                const int ax = r.x + r.w - 80;
                hits::addAction(Rect{ax - 10, y, 22, rowH - 2}, [this, i]() {
                    cursor_ = i;
                    stepRow(i, -1, false);
                });
                hits::addAction(Rect{ax + 50, y, 24, rowH - 2}, [this, i]() {
                    cursor_ = i;
                    stepRow(i, 1, false);
                });
            }
            int lx = r.x + 20;
            if (row.icon && icons::draw(c, row.icon, lx - 2, y + 2, 15)) lx += 17;
            if (row.token && d_.player >= 0) {
                const Player& tp = g_.players[size_t(d_.player)];
                cards::commandToken(c, tp.faction, theme::player[tp.color % 8], lx + 7, y + 10, 17, 255, row.token == 2);
                lx += 20;
            }
            drawText(c, FONT_SM, lx, y + 3, fitText(FONT_SM, row.label, r.x + 210 - lx), sel ? theme::text : rgb(190, 196, 212));
            if (row.res >= 0 && icons::available()) {
                drawResourceIcon(c, r.x + 226, y + 10, 18, row.res, FONT_XS, false);
                drawResourceIcon(c, r.x + 250, y + 10, 18, row.inf, FONT_XS, true);
            } else if (!row.sub.empty()) {
                drawText(c, FONT_XS, r.x + 214, y + 4, fitText(FONT_XS, row.sub, 100), theme::faint);
            }
            std::string val = row.choices.empty() ? std::to_string(row.value) : row.choices[row.value];
            int vx = r.x + r.w - 80;
            uint32_t vc = (row.choices.empty() && row.value == 0) ? theme::faint : theme::gold;
            drawTextAligned(c, FONT_MDB, vx, y + 1, 60, Align::Center, fitText(FONT_MDB, val, 58), vc);
            if (sel) {
                drawText(c, FONT_SM, vx - 4, y + 3, "\xE2\x97\x80", theme::dim);
                drawText(c, FONT_SM, vx + 58, y + 3, "\xE2\x96\xB6", theme::dim);
            }
            y += rowH;
        }
        int sy = bottom - sumH - errH + 4;
        if (!sum.empty()) {
            c.hline(r.x + 10, r.x + r.w - 10, sy - 4, theme::border);
            for (const std::string& s : sum) {
                drawText(c, FONT_SM, r.x + 14, sy, fitText(FONT_SM, s, r.w - 28), theme::text);
                sy += 15;
            }
        }
        if (!error_.empty()) drawError(c, error_, bottom);
        else drawError(c, live_, bottom, true);
        if (!warn_.empty()) {
            const std::vector<std::string> lines = wrapText(FONT_SM, warn_, r.w - 60);
            const int bh = 54 + int(lines.size()) * 15;
            const Rect b{r.x + 20, r.y + r.h / 2 - bh / 2, r.w - 40, bh};
            c.fillRoundRect(b.x, b.y, b.w, b.h, 10, rgb(14, 18, 32), 245);
            c.frameRoundRect(b.x, b.y, b.w, b.h, 10, rgb(232, 170, 60), 2);
            int ly = b.y + 10;
            for (const std::string& l : lines) {
                drawTextAligned(c, FONT_SM, b.x, ly, b.w, Align::Center, l, rgb(255, 225, 150));
                ly += 15;
            }
            hits::addButton(Rect{b.x, b.y, b.w / 2, b.h}, Btn::A);
            hits::addButton(Rect{b.x + b.w / 2, b.y, b.w / 2, b.h}, Btn::B);
            drawTextAligned(c, FONT_SMB, b.x, b.y + b.h - 24, b.w, Align::Center, keyText("A: Go ahead     B: Back"), theme::text);
        }
    }

protected:
    std::string warn_;  // 0.43.0: a legal answer that costs something, waiting for A
    virtual std::string confirmWarning() const { return ""; }
    virtual void buildAnswer() = 0;
    virtual std::vector<std::string> summary() { return {}; }
    virtual bool hasSuggestion() const { return false; }
    virtual void suggest() {}
    virtual bool onBack() { return false; }  // true = answer ready (e.g. decline)
    virtual bool onBackAvailable() const { return false; }  // onBack() would answer
    // Lets a widget limit a row by the others (e.g. a shared pool of tokens).
    virtual int clampValue(int row, int proposed) { return proposed; }
    virtual std::string confirmLabel() const { return "Confirm"; }
    virtual std::string clearLabel() const { return "Clear"; }
    virtual std::string declineLabel() const { return "Decline"; }
    virtual std::string emptyText() const { return keyText("Nothing to choose. Press START."); }

    void changed() {
        error_.clear();
        buildAnswer();
        live_ = d_.validate ? d_.validate(answer_) : "";
    }
    // a row's value up or down (wrap: A at the top goes back to the bottom)
    void stepRow(int i, int step, bool wrap) {
        if (i < 0 || i >= int(rows_.size()) || rows_[size_t(i)].header) return;
        Row& r = rows_[size_t(i)];
        if (!r.choices.empty()) {
            int n = int(r.choices.size());
            r.value = ((r.value + (step > 0 ? 1 : -1)) % n + n) % n;
        } else {
            int nv = std::max(r.min, std::min(r.max, r.value + step));
            if (wrap && r.value == r.max) nv = r.min;
            r.value = clampValue(i, nv);
        }
        changed();
    }
    void moveCursor(int dir) {
        int n = int(rows_.size());
        for (int k = 0; k < n; ++k) {
            cursor_ = (cursor_ + dir + n) % n;
            if (!rows_[cursor_].header) return;
        }
    }
    void firstRow() {
        cursor_ = 0;
        while (cursor_ < int(rows_.size()) && rows_[cursor_].header) ++cursor_;
        if (cursor_ >= int(rows_.size())) cursor_ = 0;
    }

    std::vector<Row> rows_;
    int cursor_ = 0, scroll_ = 0;
    std::string live_;  // live validation message
};

// ------------------------------------------------------------------- pay
class PayUI : public RowsUI {
public:
    PayUI(const Game& g, const Decision& d) : RowsUI(g, d) {
        for (int x : d_.planets) {
            Row r;
            r.label = kPlanets[g.planets[x].def].name;
            r.sub = std::to_string(planetResources(g, x)) + " res / " + std::to_string(planetInfluence(g, x)) + " inf";
            r.res = planetResources(g, x);
            r.inf = planetInfluence(g, x);
            r.choices = {"-", "Use"};
            r.a = x;
            rows_.push_back(r);
        }
        if (g.players[d.player].tradeGoods > 0 && d.allowTradeGoods) {
            Row r;
            r.label = "Trade goods";
            r.icon = "trade_good";
            r.sub = "have " + std::to_string(g.players[d.player].tradeGoods) +
                    (tgWorth(g, d.player) > 1 ? " (worth 2 each: Mirror Computing)" : "");
            r.max = g.players[d.player].tradeGoods;
            r.a = -1;
            rows_.push_back(r);
        }
        suggest();
        firstRow();
        changed();
    }

protected:
    void buildAnswer() override {
        answer_ = Answer();
        for (const Row& r : rows_) {
            if (r.a >= 0 && r.value == 1) answer_.picks.push_back(r.a);
            if (r.a == -1) answer_.number = r.value;
        }
    }
    std::vector<std::string> summary() override {
        int total = 0;
        for (const Row& r : rows_) {
            if (r.a >= 0 && r.value == 1) total += planetPayValue(g_, r.a, d_.influence, d_.mixed);
            if (r.a == -1) total += r.value * (d_.allowTradeGoods ? tgWorth(g_, d_.player) : 1);
        }
        if (d_.tag == Tag::VotePlanets) return {"Votes: " + std::to_string(total)};
        return {"Paying " + std::to_string(total) + " of " + std::to_string(d_.amount) +
                (d_.mixed ? " (resources or influence)" : d_.influence ? " influence" : " resources") +
                (d_.amount2 ? " (then " + std::to_string(d_.amount2) + " influence)" : "")};
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Rng rng(3);
        Answer s = d_.tag == Tag::VotePlanets ? aiAnswer(g_, d_, rng) : aiPayment(g_, d_);
        for (Row& r : rows_) {
            if (r.a >= 0) r.value = std::find(s.picks.begin(), s.picks.end(), r.a) != s.picks.end() ? 1 : 0;
            if (r.a == -1) r.value = std::min(r.max, std::max(0, s.number));
        }
    }
    bool onBackAvailable() const override { return d_.maxPick == 1; }
    bool onBack() override {
        if (d_.maxPick == 1) {  // optional payment: decline
            answer_ = Answer();
            answer_.number = -1;
            return true;
        }
        return false;
    }
    std::string confirmLabel() const override { return d_.tag == Tag::VotePlanets ? "Vote" : "Pay"; }
    std::string clearLabel() const override { return "Clear"; }
    std::string declineLabel() const override { return d_.tag == Tag::VotePlanets ? "No votes" : "Don't pay"; }
};

// ------------------------------------------------------------------- tokens
class TokensUI : public RowsUI {
public:
    TokensUI(const Game& g, const Decision& d) : RowsUI(g, d) {
        const Player& p = g.players[d.player];
        const char* names[3] = {"Tactic pool", "Fleet pool", "Strategy pool"};
        const char* subs[3] = {"activations", "ships per system", "secondary abilities"};
        int base[3] = {p.tactic, p.fleet, p.strategy};
        total_ = base[0] + base[1] + base[2] + d.newTokens;
        for (int i = 0; i < 3; ++i) {
            Row r;
            r.label = names[i];
            r.sub = subs[i];
            r.min = d.redistribute ? 0 : base[i];
            r.max = total_;
            r.value = base[i];
            r.a = i;
            r.token = i == 1 ? 2 : 1;  // the fleet pool's tokens sit ship side up
            rows_.push_back(r);
        }
        rows_[0].value += d.newTokens;  // default: new tokens go to the tactic pool
        firstRow();
        changed();
    }

protected:
    void buildAnswer() override {
        answer_ = Answer();
        answer_.data = {rows_[0].value, rows_[1].value, rows_[2].value};
    }
    std::vector<std::string> summary() override {
        int sum = rows_[0].value + rows_[1].value + rows_[2].value;
        int left = total_ - sum;
        if (left > 0) return {"Tokens left to place: " + std::to_string(left)};
        return {"All " + std::to_string(total_) + keyText(" tokens placed - press START")};
    }
    int clampValue(int row, int proposed) override {
        int others = 0;
        for (int i = 0; i < 3; ++i)
            if (i != row) others += rows_[i].value;
        return std::min(proposed, total_ - others);
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Answer s = aiTokens(g_, d_);
        for (int i = 0; i < 3; ++i) rows_[i].value = s.data[i];
    }
    std::string clearLabel() const override { return "Reset"; }

private:
    int total_ = 0;
};

// ------------------------------------------------------------------- hits
class HitsUI : public RowsUI {
public:
    HitsUI(const Game& g, const Decision& d) : RowsUI(g, d) {
        const Force& f = d.planet >= 0 ? g.planets[d.planet].units[d.player] : g.systems[d.system].space[d.player];
        const bool remove = d.noSustain;
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t] || (d.hitMask && !((d.hitMask >> t) & 1))) continue;
            UnitType ut = UnitType(t);
            if (!remove && unitSustainsAt(g, d.player, ut, d.system, d.planet) && f.n[t] - f.dmg[t] > 0) {
                Row r;
                r.label = std::string("Sustain: ") + unitLabel(g, d.player, ut);
                r.sub = std::to_string(f.n[t] - f.dmg[t]) + " undamaged";
                r.max = f.n[t] - f.dmg[t];
                r.a = t;
                r.b = 1;
                rows_.push_back(r);
            }
            Row r;
            r.label = std::string(remove ? "Remove: " : "Destroy: ") + unitLabel(g, d.player, ut);
            r.sub = std::to_string(f.n[t]) + " here";
            if (f.dmg[t]) r.sub += ", " + std::to_string(f.dmg[t]) + " damaged";
            r.max = f.n[t];
            r.a = t;
            r.b = 0;
            rows_.push_back(r);
        }
        need_ = std::min(d.hits, maxAbsorbableHits(g, d.player, d.system, d.planet, d.hitMask, d.noSustain));
        suggest();
        firstRow();
        changed();
    }

protected:
    void buildAnswer() override {
        answer_ = Answer();
        for (const Row& r : rows_)
            for (int i = 0; i < r.value; ++i) answer_.data.insert(answer_.data.end(), {r.a, r.b});
    }
    // hits one entry of this row absorbs (Sustain Damage: 2 with Non-Euclidean Shielding)
    int worth(const Row& r) const { return r.b == 1 && !d_.noSustain ? sustainWorth(g_, d_.player) : 1; }
    std::vector<std::string> summary() override {
        int sum = 0;
        for (const Row& r : rows_) sum += r.value * worth(r);
        return {std::string(d_.noSustain ? "Units chosen: " : "Hits assigned: ") + std::to_string(std::min(sum, need_)) + " of " +
                std::to_string(need_)};
    }
    int clampValue(int row, int proposed) override {
        int others = 0;
        for (int i = 0; i < int(rows_.size()); ++i)
            if (i != row) others += rows_[i].value * worth(rows_[i]);
        int w = worth(rows_[row]);
        int v = std::min(proposed, (need_ - others + w - 1) / w);
        return std::max(0, v);
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Answer s = aiHits(g_, d_);
        for (Row& r : rows_) r.value = 0;
        for (size_t i = 0; i < s.data.size(); i += 2)
            for (Row& r : rows_)
                if (r.a == s.data[i] && r.b == s.data[i + 1]) ++r.value;
    }

private:
    int need_ = 0;
};

// ------------------------------------------------------------------- land
// Landing ground forces on planets, or choosing bombardment targets.
class LandUI : public RowsUI {
public:
    LandUI(const Game& g, const Decision& d) : RowsUI(g, d) {
        const SystemState& s = g.systems[d.system];
        bombard_ = d.tag == Tag::BombardPlan;
        int mask = d.hitMask ? d.hitMask : ((1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)));
        for (int16_t pl : s.planets) {
            if (!d.planets.empty() && std::find(d.planets.begin(), d.planets.end(), int(pl)) == d.planets.end()) continue;
            if (!bombard_ && isDMZ(g, pl)) continue;  // Demilitarized Zone: nothing can land
            Row h;
            h.header = true;
            int owner = g.planets[pl].owner;
            h.label = std::string(kPlanets[g.planets[pl].def].name) +
                      (owner >= 0 ? " (" + (owner == d.player ? std::string("yours") : playerName(g, owner)) + ")" : " (unclaimed)");
            int def = 0;
            for (int q = 0; q < g.numPlayers; ++q)
                if (q != d.player) def += g.planets[pl].units[q].groundForces();
            if (def) h.label += " - " + std::to_string(def) + (bombard_ ? " enemy ground forces" : " defender(s)");
            if (pl == d.planet && !bombard_) h.label += " - land here";
            rows_.push_back(h);
            for (int t = 0; t < kUnitTypes; ++t) {
                if (!((mask >> t) & 1)) continue;
                int have = bombard_ ? bombardAvailable(g, d.player, d.system, UnitType(t)) : int(s.space[d.player].n[t]);
                if (!have) continue;
                const UnitDef& u = unitDef(g, d.player, UnitType(t));
                Row r;
                r.label = std::string("  ") + unitLabel(g, d.player, UnitType(t), true);
                if (bombard_)
                    r.sub = std::to_string(have) + " can fire (hit on " + std::to_string(u.bombard) + "+" +
                            (u.bombardDice > 1 ? ", " + std::to_string(u.bombardDice) + " dice" : "") + ")";
                else
                    r.sub = std::to_string(have) + " in space";
                r.max = have;
                r.a = pl;
                r.b = t;
                rows_.push_back(r);
            }
        }
        suggest();
        firstRow();
        changed();
    }

protected:
    void buildAnswer() override {
        answer_ = Answer();
        for (const Row& r : rows_)
            if (!r.header && r.value > 0) answer_.data.insert(answer_.data.end(), {r.a, r.b, r.value});
    }
    std::vector<std::string> summary() override {
        int used[kUnitTypes] = {};
        for (const Row& r : rows_)
            if (!r.header) used[r.b] += r.value;
        const Force& f = g_.systems[d_.system].space[d_.player];
        std::string line = bombard_ ? "Firing: " : "Landing: ";
        bool any = false;
        for (int t = 0; t < kUnitTypes; ++t) {
            bool listed = false;
            for (const Row& r : rows_)
                if (!r.header && r.b == t) listed = true;
            if (!listed) continue;
            const int have = bombard_ ? bombardAvailable(g_, d_.player, d_.system, UnitType(t)) : int(f.n[t]);
            line += std::string(any ? ", " : "") + std::to_string(used[t]) + "/" + std::to_string(have) + " " +
                    unitLabel(g_, d_.player, UnitType(t), true);
            any = true;
        }
        return {line};
    }
    int clampValue(int row, int proposed) override {
        int others = 0;
        for (int i = 0; i < int(rows_.size()); ++i)
            if (i != row && !rows_[i].header && rows_[i].b == rows_[row].b) others += rows_[i].value;
        return std::min(proposed, rows_[row].max - others);
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Decision d = d_;
        Answer s = aiAnswer(g_, d, rng_);
        for (Row& r : rows_)
            if (!r.header) r.value = 0;
        for (size_t i = 0; i + 2 < s.data.size(); i += 3) {
            for (Row& r : rows_)
                if (!r.header && r.a == s.data[i] && r.b == s.data[i + 1]) r.value = std::min(r.max, s.data[i + 2]);
        }
    }
    std::string confirmLabel() const override { return bombard_ ? "Fire" : "Land"; }
    std::string emptyText() const override { return "No planets here."; }

private:
    bool bombard_ = false;
    Rng rng_{7};
};

// ------------------------------------------------------------------- produce
class ProduceUI : public RowsUI {
public:
    ProduceUI(const Game& g, const Decision& d) : RowsUI(g, d) {
        const int p = d.player;
        bool enemyShips = blockaded(g, p, d.system);
        for (int t = 0; t < kUnitTypes; ++t) {
            UnitType ut = UnitType(t);
            if (isStructure(ut)) continue;
            if (enemyShips && isShip(ut)) continue;  // blockaded: ground forces only
            if (d.hitMask && !((d.hitMask >> t) & 1)) continue;  // ships only / Umbat
            int avail = std::min(unitsAvailable(g, p, ut), d.amount);
            // 0.44.4: Bellum Gloriosum (Sol breakthrough) - ground forces and fighters for the new ships' capacity don't
            // count against the production limit, so the row isn't capped by it (the rules check the total)
            if ((isGroundForce(ut) || ut == UnitType::Fighter) && breakthroughIs(g, p, "sol")) avail = unitsAvailable(g, p, ut);
            if (avail <= 0) continue;
            const UnitDef& u = unitDef(g, p, ut);
            if (u.cost2 <= 0) continue;
            Row r;
            r.label = u.name;
            int c2 = unitCost2(g, p, ut);
            r.sub = c2 == 1 ? "1 res for 2" : std::to_string((c2 + 1) / 2) + " res";
            r.max = avail;
            r.a = t;
            rows_.push_back(r);
        }
        // where new ground forces go (Integrated Economy: that planet - after 0.44.4)
        std::vector<int> dest = d.planet >= 0 ? std::vector<int>{d.planet} : groundForceDestinations(g, p, d.system);
        if (!dest.empty()) {
            Row r;
            r.label = "Ground forces go to";
            r.a = -1;
            for (int x : dest) {
                r.choices.push_back(x >= 0 ? kPlanets[g.planets[x].def].name : "Space");
                destIds_.push_back(x);
            }
            rows_.push_back(r);
        }
        if (enemyShips)
            d_.context += " Blockaded by enemy ships: only ground forces can be produced.";
        firstRow();
        changed();
    }

protected:
    void buildAnswer() override {
        answer_ = Answer();
        answer_.data.assign(kUnitTypes + 1, 0);
        answer_.data[kUnitTypes] = destIds_.empty() ? -1 : destIds_[0];
        for (const Row& r : rows_) {
            if (r.a >= 0) answer_.data[r.a] = r.value;
            else answer_.data[kUnitTypes] = destIds_[r.value];
        }
    }
    std::vector<std::string> summary() override {
        int counts[kUnitTypes] = {};
        int units = 0;
        for (const Row& r : rows_)
            if (r.a >= 0) {
                counts[r.a] = r.value;
                units += r.value;
            }
        int cost = std::max(0, productionCost(g_, d_.player, counts) - d_.value) + terrafactoryExtra(g_, d_.player, d_.system, counts);
        if (d_.planet >= 0 && d_.costCap > 0)  // Integrated Economy (after 0.44.4): the cost limit, not a unit count
            return {"Combined cost " + std::to_string(unitsCostValue(g_, d_.player, counts)) + " / " + std::to_string(d_.costCap) + "    Pay " +
                    std::to_string(cost) + " (you can spend " + std::to_string(spendable(g_, d_.player, false)) + ")"};
        return {"Units " + std::to_string(units) + " / " + std::to_string(d_.amount) + "    Cost " + std::to_string(cost) +
                " (you can spend " + std::to_string(spendable(g_, d_.player, false)) + ")"};
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Answer s = aiAnswer(g_, d_, rng_);
        for (Row& r : rows_) {
            if (r.a >= 0) r.value = std::min(r.max, s.data[r.a]);
            else
                for (size_t i = 0; i < destIds_.size(); ++i)
                    if (destIds_[i] == s.data[kUnitTypes]) r.value = int(i);
        }
    }
    std::string confirmLabel() const override { return "Produce"; }

protected:
    std::vector<int> destIds_;
    Rng rng_{11};
};

// ------------------------------------------------------------------- radial menus (0.58.4)
// Shared drawing for the owner's wheel menus: angles in radians, 0 = right, growing clockwise on screen.
namespace radial {
constexpr float kPi = 3.14159265f;
// a ring slice from r0 to r1 between angles a0 and a1
void slice(Canvas& c, float cx, float cy, float r0, float r1, float a0, float a1, uint32_t col, int alpha) {
    const int steps = std::max(2, int((a1 - a0) / (kPi / 36.f)) + 1);
    float xs[160], ys[160];
    int n = 0;
    for (int i = 0; i <= steps && n < 78; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / float(steps);
        xs[n] = cx + std::cos(a) * r1;
        ys[n++] = cy + std::sin(a) * r1;
    }
    for (int i = steps; i >= 0 && n < 158; --i) {
        const float a = a0 + (a1 - a0) * float(i) / float(steps);
        xs[n] = cx + std::cos(a) * r0;
        ys[n++] = cy + std::sin(a) * r0;
    }
    c.fillPolygon(xs, ys, n, col, alpha);
}
void spoke(Canvas& c, float cx, float cy, float r0, float r1, float a, uint32_t col, int alpha) {
    c.line(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * r1, cy + std::sin(a) * r1, col, 1.5f, alpha);
}
// a small solid triangle pointing up or down, centred on (x, y)
void arrow(Canvas& c, float x, float y, bool up, uint32_t col, int alpha = 255) {
    const float w = 6.f, h = 4.5f;
    const float xs[3] = {x - w, x + w, x}, ys[3] = {up ? y + h : y - h, up ? y + h : y - h, up ? y - h : y + h};
    c.fillPolygon(xs, ys, 3, col, alpha);
}
// the centre button: always lit (yellow) or grey until there is something to confirm
void centre(Canvas& c, float cx, float cy, float r, const std::string& label, const std::string& sub, bool lit, uint32_t now) {
    const uint32_t fill = lit ? rgb(234, 190, 64) : rgb(70, 74, 88);
    const uint32_t ink = lit ? rgb(36, 26, 6) : rgb(150, 154, 168);
    if (lit) {  // a slow glow round it
        const int pulse = 40 + int(30.f * (0.5f + 0.5f * std::sin(float(now % 100000u) / 300.f)));
        c.ring(cx, cy, r + 4.f, 4.f, rgb(255, 210, 90), pulse);
    }
    c.fillCircle(cx, cy, r, fill);
    c.ring(cx, cy, r, 2.f, lit ? rgb(255, 236, 160) : rgb(110, 114, 130));
    drawTextAligned(c, FONT_MDB, int(cx - r), int(cy) - (sub.empty() ? 9 : 14), int(2.f * r), Align::Center, label, ink);
    if (!sub.empty()) drawTextAligned(c, FONT_XS, int(cx - r), int(cy) + 5, int(2.f * r), Align::Center, fitText(FONT_XS, sub, int(2.f * r) - 6), ink);
}
}  // namespace radial

// 0.58.4 (owner: "Whenever the human player is producing a ship, a pop-up radial wheel menu appears. Between each
// spoke of the wheel, the ship type in that player's color shows. Next to the ship, inside the wheel menu, is the
// number window and some arrows ... The center of the wheel is a button that says Confirm. It is always yellow as
// players can choose whether or not they want to produce a unit."). Every unit type the faction can produce has a
// place (the ones it can't produce here are dimmed and say why), clockwise from the top; the ground forces'
// destination is a stop of its own under the wheel. Left / right turn, up / down (the arrows, the mouse wheel) change
// the number; the values live in ProduceUI's rows, so the answer, the suggestion and the checks are unchanged.
class ProduceWheelUI : public ProduceUI {
public:
    ProduceWheelUI(const Game& g, const Decision& d) : ProduceUI(g, d) {
        const int p = d.player;
        const bool enemyShips = blockaded(g, p, d.system);
        static const UnitType kOrder[] = {UnitType::Carrier, UnitType::Cruiser,  UnitType::Destroyer, UnitType::Dreadnought,
                                          UnitType::Flagship, UnitType::Flagship2, UnitType::WarSun,    UnitType::Fighter,
                                          UnitType::Infantry, UnitType::Mech};
        for (UnitType ut : kOrder) {
            const int t = int(ut);
            if (t >= kUnitTypes) continue;
            const UnitDef& u = unitDef(g, p, ut);
            if (u.cost2 <= 0) continue;  // (Flagship2, the Lady, is only the Ghemina's)
            Item it;
            it.type = ut;
            it.name = u.name;
            for (size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].a == t) it.row = int(i);
            if (it.row >= 0) {
                const int c2 = unitCost2(g, p, ut);
                it.why = c2 == 1 ? "1 res for 2" : std::to_string((c2 + 1) / 2) + " res";
            } else if (d.hitMask && !((d.hitMask >> t) & 1)) it.why = "not here";
            else if (enemyShips && isShip(ut)) it.why = "blockaded";
            else if (ut == UnitType::WarSun && !ownsWarSunTech(g, p)) it.why = "needs the tech";
            else it.why = "none left";
            items_.push_back(it);
        }
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].a < 0) destRow_ = int(i);
        for (size_t k = 0; k < items_.size(); ++k)
            if (items_[k].row >= 0) {
                sel_ = int(k);
                break;
            }
        if (sel_ < 0 && destRow_ >= 0) sel_ = int(items_.size());
    }

    bool update(const Input& in, uint32_t) override {
        if (!warn_.empty()) {
            if (in.pressed(Btn::A) || in.pressed(Btn::Start)) {
                warn_.clear();
                return true;
            }
            if (in.pressed(Btn::B)) warn_.clear();
            return false;
        }
        const int stops = int(items_.size()) + (destRow_ >= 0 ? 1 : 0);
        auto turn = [&](int dir) {
            for (int k = 1; k <= stops; ++k) {
                const int s = ((sel_ + dir * k) % stops + stops) % stops;
                if (rowOf(s) >= 0) {
                    sel_ = s;
                    return;
                }
            }
        };
        if (stops > 0) {
            if (in.pressed(Btn::Right)) turn(1);
            if (in.pressed(Btn::Left)) turn(-1);
        }
        const bool kb = kbConfirm();
        const int row = rowOf(sel_);
        if (row >= 0) {
            int step = 0;
            if (in.pressed(Btn::Up) || in.pressed(Btn::R1) || (!kb && in.pressed(Btn::A))) step = 1;
            if (in.pressed(Btn::Down) || in.pressed(Btn::L1)) step = -1;
            if (step) stepRow(row, step, !kb && in.pressed(Btn::A));
        }
        if (in.pressed(Btn::Y)) {
            suggest();
            changed();
        }
        if (in.pressed(Btn::B)) {
            for (Row& x : rows_)
                if (!x.header && x.choices.empty()) x.value = x.min;
            changed();
        }
        if (in.pressed(Btn::Start) || (kb && in.pressed(Btn::A))) {
            buildAnswer();
            const std::string e = d_.validate ? d_.validate(answer_) : "";
            if (e.empty()) {
                warn_ = confirmWarning();
                return warn_.empty();
            }
            setError(e);
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        std::vector<std::pair<std::string, std::string>> h = {{"\xE2\x97\x80\xE2\x96\xB6", "Unit"}, {"\xE2\x96\xB2\xE2\x96\xBC", "How many"}};
        h.push_back({kbConfirm() ? "A" : "START", "Confirm"});
        h.push_back({"Y", "Suggest"});
        if (backAvailable()) h.push_back({"B", "Clear"});
        return h;
    }
    Rect area() const override { return Rect{6, 26, 410, 420}; }

    void draw(Canvas& c, uint32_t now) override {
        const Rect a = area();
        const int p = d_.player;
        const uint32_t pc = p >= 0 && p < g_.numPlayers ? theme::player[g_.players[size_t(p)].color % 8] : theme::gold;
        c.fillRoundRect(a.x, a.y, a.w, a.h, 10, rgb(8, 11, 22), 214);
        c.frameRoundRect(a.x, a.y, a.w, a.h, 10, theme::border);
        hits::addAction(a, [] {});  // (a click inside the window is not for the map)
        drawText(c, FONT_MDB, a.x + 12, a.y + 6, fitText(FONT_MDB, d_.prompt, a.w - 24), theme::text);
        int ty = a.y + 26;
        for (const std::string& l : wrapText(FONT_XS, d_.context, a.w - 24)) {
            if (ty > a.y + 40) break;
            drawText(c, FONT_XS, a.x + 12, ty, l, theme::dim);
            ty += 12;
        }
        const float cx = float(a.x + a.w / 2), cy = float(a.y + 214);
        const float R0 = 48.f, R1 = 158.f;
        const int n = std::max(1, int(items_.size()));
        const float span = 2.f * radial::kPi / float(n);
        c.fillCircle(cx, cy, R1 + 3.f, rgb(4, 6, 14), 200);
        for (int k = 0; k < int(items_.size()); ++k) {  // the slices
            const Item& it = items_[size_t(k)];
            const float mid = -radial::kPi / 2.f + span * float(k);
            const bool on = k == sel_, avail = it.row >= 0;
            const int v = avail ? rows_[size_t(it.row)].value : 0;
            const uint32_t fill = on ? mix(theme::panelHi, pc, 70) : v > 0 ? mix(rgb(16, 20, 36), pc, 40) : rgb(14, 18, 32);
            radial::slice(c, cx, cy, R0, R1, mid - span / 2.f, mid + span / 2.f, fill, avail ? 240 : 170);
        }
        for (int k = 0; k < n && items_.size() > 1; ++k)
            radial::spoke(c, cx, cy, R0, R1, -radial::kPi / 2.f + span * (float(k) - 0.5f), theme::border, 255);
        c.ring(cx, cy, R1, 2.f, theme::border);
        if (sel_ >= 0 && sel_ < int(items_.size())) {  // the chosen slice's rim in gold
            const float mid = -radial::kPi / 2.f + span * float(sel_);
            for (float t = -span / 2.f; t < span / 2.f; t += 0.02f)
                c.fillCircle(cx + std::cos(mid + t) * R1, cy + std::sin(mid + t) * R1, 1.6f, theme::gold);
        }
        const bool kb = kbConfirm();
        for (int k = 0; k < int(items_.size()); ++k) {
            const Item& it = items_[size_t(k)];
            const float mid = -radial::kPi / 2.f + span * float(k);
            const float ux = std::cos(mid), uy = std::sin(mid);
            const bool avail = it.row >= 0, on = k == sel_;
            // the unit, in the player's colour
            const float ax = cx + ux * 110.f, ay = cy + uy * 110.f;
            const float len = it.type == UnitType::WarSun || it.type == UnitType::Flagship || it.type == UnitType::Flagship2 ? 46.f
                              : it.type == UnitType::Fighter || isGroundForce(it.type)                             ? 26.f
                                                                                                                    : 38.f;
            tileview::drawUnit(c, it.type, ax, ay, len, pc, false, avail ? 255 : 70, false, now, isGroundForce(it.type));
            // its name and price on the picture's outer side (above it in the top half, below it in the bottom half)
            const float halfH = it.type == UnitType::WarSun ? 22.f : isGroundForce(it.type) ? 14.f : it.type == UnitType::Fighter ? 8.f : 13.f;
            const int ty = uy < 0.f ? int(ay - halfH) - 25 : int(ay + halfH) + 2;
            drawTextAligned(c, FONT_XS, int(ax) - 50, ty, 100, Align::Center, fitText(FONT_XS, it.name, 96),
                            avail ? (on ? theme::text : rgb(196, 202, 218)) : theme::faint);
            drawTextAligned(c, FONT_XS, int(ax) - 50, ty + 11, 100, Align::Center, it.why, avail ? theme::faint : rgb(150, 110, 110));
            // the number window and its arrows, toward the centre
            const float bx = cx + ux * 70.f, by = cy + uy * 70.f;
            const int k2 = k;
            if (avail) {
                const int v = rows_[size_t(it.row)].value;
                c.fillRoundRect(int(bx) - 14, int(by) - 10, 28, 20, 5, rgb(5, 7, 14), 245);
                c.frameRoundRect(int(bx) - 14, int(by) - 10, 28, 20, 5, on ? theme::gold : theme::border, on ? 2 : 1);
                drawTextAligned(c, FONT_MDB, int(bx) - 14, int(by) - 9, 28, Align::Center, std::to_string(v), v > 0 ? theme::gold : theme::faint);
                const bool canUp = v < rows_[size_t(it.row)].max, canDown = v > rows_[size_t(it.row)].min;
                radial::arrow(c, bx, by - 16.f, true, canUp ? (on ? theme::text : theme::dim) : theme::faint, canUp ? 255 : 90);
                radial::arrow(c, bx, by + 16.f, false, canDown ? (on ? theme::text : theme::dim) : theme::faint, canDown ? 255 : 90);
                // the mouse: point at the unit to pick it; a click on it adds one (with keyboard & mouse), the arrows step
                if (kb)
                    hits::addAction(Rect{int(ax) - 28, int(ay) - 22, 56, 54}, [this, k2]() {
                        if (sel_ == k2) stepRow(rowOf(k2), 1, true);
                        else sel_ = k2;
                    });
                else hits::add(Rect{int(ax) - 28, int(ay) - 22, 56, 54}, [this, k2]() { sel_ = k2; });
                hits::addAction(Rect{int(bx) - 12, int(by) - 23, 24, 12}, [this, k2]() {
                    sel_ = k2;
                    stepRow(rowOf(k2), 1, false);
                });
                hits::addAction(Rect{int(bx) - 12, int(by) + 11, 24, 12}, [this, k2]() {
                    sel_ = k2;
                    stepRow(rowOf(k2), -1, false);
                });
                hits::addAction(Rect{int(bx) - 14, int(by) - 10, 28, 20}, [this, k2]() { sel_ = k2; });
            } else {
                drawTextAligned(c, FONT_MDB, int(bx) - 14, int(by) - 9, 28, Align::Center, "-", theme::faint);
            }
        }
        // the centre: Confirm, always lit (producing nothing is allowed)
        int counts[kUnitTypes] = {};
        int units = 0;
        for (const Row& r : rows_)
            if (r.a >= 0) {
                counts[r.a] = r.value;
                units += r.value;
            }
        const int cost = std::max(0, productionCost(g_, d_.player, counts) - d_.value) + terrafactoryExtra(g_, d_.player, d_.system, counts);
        radial::centre(c, cx, cy, 42.f, "Confirm", units ? std::to_string(units) + (units == 1 ? " unit, " : " units, ") + std::to_string(cost) + " res" : "nothing",
                       true, now);
        hits::addButton(Rect{int(cx) - 40, int(cy) - 40, 80, 80}, Btn::Start);
        // under the wheel: where the ground forces go, the totals, the checks
        int y = int(cy + R1) + 8;
        if (destRow_ >= 0) {
            const Row& r = rows_[size_t(destRow_)];
            const bool on = sel_ == int(items_.size());
            const std::string label = "Ground forces go to:";
            const std::string val = r.choices.empty() ? "" : r.choices[size_t(r.value)];
            const int lw = textWidth(FONT_SM, label), vw = std::min(170, textWidth(FONT_SMB, val));
            const int x0 = a.x + (a.w - (lw + vw + 46)) / 2;
            if (on) c.fillRoundRect(x0 - 8, y - 2, lw + vw + 62, 18, 5, theme::panelHi, 245);
            drawText(c, FONT_SM, x0, y, label, on ? theme::text : theme::dim);
            const int vx = x0 + lw + 22;
            drawText(c, FONT_SM, vx - 14, y, "\xE2\x97\x80", on ? theme::text : theme::faint);
            drawText(c, FONT_SMB, vx, y, fitText(FONT_SMB, val, 170), theme::gold);
            drawText(c, FONT_SM, vx + vw + 6, y, "\xE2\x96\xB6", on ? theme::text : theme::faint);
            const int dsel = int(items_.size());
            hits::add(Rect{x0 - 8, y - 2, lw + 14, 18}, [this, dsel]() { sel_ = dsel; });
            hits::addAction(Rect{vx - 18, y - 2, 18, 18}, [this, dsel]() {
                sel_ = dsel;
                stepRow(destRow_, -1, false);
            });
            hits::addAction(Rect{vx, y - 2, vw + 20, 18}, [this, dsel]() {
                sel_ = dsel;
                stepRow(destRow_, 1, false);
            });
            y += 18;
        }
        const std::vector<std::string> sum = summary();
        for (const std::string& l : sum) {
            drawTextAligned(c, FONT_SM, a.x, y, a.w, Align::Center, fitText(FONT_SM, l, a.w - 20), theme::text);
            y += 14;
        }
        const std::string& msg = !error_.empty() ? error_ : live_;
        if (!msg.empty())
            drawTextAligned(c, FONT_XS, a.x, y + 1, a.w, Align::Center, fitText(FONT_XS, msg, a.w - 20), !error_.empty() ? theme::red : rgb(232, 170, 60));
        if (!warn_.empty()) {
            const std::vector<std::string> lines = wrapText(FONT_SM, warn_, a.w - 60);
            const int bh = 54 + int(lines.size()) * 15;
            const Rect b{a.x + 20, int(cy) - bh / 2, a.w - 40, bh};
            c.fillRoundRect(b.x, b.y, b.w, b.h, 10, rgb(14, 18, 32), 245);
            c.frameRoundRect(b.x, b.y, b.w, b.h, 10, rgb(232, 170, 60), 2);
            int ly = b.y + 10;
            for (const std::string& l : lines) {
                drawTextAligned(c, FONT_SM, b.x, ly, b.w, Align::Center, l, rgb(255, 225, 150));
                ly += 15;
            }
            hits::addButton(Rect{b.x, b.y, b.w / 2, b.h}, Btn::A);
            hits::addButton(Rect{b.x + b.w / 2, b.y, b.w / 2, b.h}, Btn::B);
            drawTextAligned(c, FONT_SMB, b.x, b.y + b.h - 24, b.w, Align::Center, keyText("A: Go ahead     B: Back"), theme::text);
        }
    }

private:
    struct Item {
        UnitType type = UnitType::Carrier;
        std::string name, why;
        int row = -1;  // its row in ProduceUI (-1: can't be produced here)
    };
    int rowOf(int s) const {
        if (s >= 0 && s < int(items_.size())) return items_[size_t(s)].row;
        if (s == int(items_.size())) return destRow_;
        return -1;
    }
    std::vector<Item> items_;
    int destRow_ = -1;
    int sel_ = -1;
};

// ------------------------------------------------------------------- move
class MoveUI : public RowsUI {
public:
    MoveUI(const Game& g, const Decision& d, MapView& map) : RowsUI(g, d), map_(map) {
        const int p = d.player, act = d.system;
        struct Carrier {
            int sys, mv;
        };
        std::vector<Carrier> carriers;
        std::vector<int> shipSystems;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (int(s) == act || (g.systems[s].hasToken(p) && !g.fx.dominusOrb && !(heroActive(g, p, "nomad") && flagshipIn(g, p, int(s)))) ||
                g.systems[s].isHyperlane())
                continue;
            const Force& f = g.systems[s].space[p];
            bool anyShip = false;
            for (int t : kShipIdx) {
                if (!f.n[t] || t == int(UnitType::Fighter)) continue;
                if (!shipReach(g, p, int(s), act, UnitType(t), moveBonusMax(g, p)).ok) continue;
                anyShip = true;
                if (unitDef(g, p, UnitType(t)).capacity > 0)
                    carriers.push_back({int(s), moveValue(g, p, int(s), UnitType(t)) + moveBonusMax(g, p)});
            }
            if (f.n[int(UnitType::Fighter)] && shipReach(g, p, int(s), act, UnitType::Fighter, moveBonusMax(g, p)).ok) anyShip = true;
            // Floating factories (Saar) move and carry like ships
            if (f.count(UnitType::SpaceDock) && movesAsShip(g, p, UnitType::SpaceDock) &&
                shipReach(g, p, int(s), act, UnitType::SpaceDock, moveBonusMax(g, p)).ok) {
                anyShip = true;
                carriers.push_back({int(s), moveValue(g, p, int(s), UnitType::SpaceDock) + moveBonusMax(g, p)});
            }
            if (anyShip) shipSystems.push_back(int(s));
        }
        for (int s : shipSystems) addSystemRows(s, false);
        shipSystems_ = shipSystems;
        // Systems a moving carrier passes through: fighters and ground forces can be picked up there.
        for (size_t s = 0; s < g.systems.size(); ++s) {
            if (int(s) == act || (g.systems[s].hasToken(p) && !g.fx.dominusOrb && !(heroActive(g, p, "nomad") && flagshipIn(g, p, int(s)))) ||
                g.systems[s].isHyperlane())
                continue;
            if (std::find(shipSystems.begin(), shipSystems.end(), int(s)) != shipSystems.end()) continue;
            if (!passableFor(g, p, int(s))) continue;
            bool units = g.systems[s].space[p].count(UnitType::Fighter) || g.systems[s].space[p].groundForces();
            for (int16_t pl : g.systems[s].planets)
                if (g.planets[pl].units[p].groundForces()) units = true;
            if (!units) continue;
            bool onRoute = false;
            for (const Carrier& c : carriers)
                if (travelCost(g, p, c.sys, int(s)) + travelCost(g, p, int(s), act) <= c.mv) onRoute = true;
            if (onRoute) addSystemRows(int(s), true);
        }
        // 0.30.0 (owner, LRR 95.1): ground forces on the active system's planets can be picked up by the
        // ships moving in, then landed on another planet of that system
        if (!carriers.empty() && act >= 0) {
            bool header = false;
            for (int16_t pl : g.systems[act].planets) {
                const Force& pf = g.planets[pl].units[p];
                for (UnitType ut : {UnitType::Infantry, UnitType::Mech}) {
                    if (!pf.count(ut)) continue;
                    if (!header) {
                        Row h;
                        h.header = true;
                        h.label = "Pick up in " + systemLabel(act) + " (to land elsewhere there)";
                        rows_.push_back(h);
                        header = true;
                    }
                    Row r;
                    r.label = std::string("  ") + unitTypeName(ut, true) + " on " + kPlanets[g.planets[pl].def].name;
                    r.sub = std::to_string(pf.count(ut)) + " there, carried";
                    r.max = pf.count(ut);
                    r.a = act;
                    r.b = pl;
                    r.c = int(ut);
                    rows_.push_back(r);
                }
            }
        }
        addUnreachable();
        firstRow();
        changed();
    }
    ~MoveUI() override {
        map_.clearRoutes();
        if (wp_) map_.clearHighlights();
    }

    // ---- Waypoint Mode (0.56.0, owner: "a button option on the bottom right ... a cross-hair as its icon"): the ships
    // leaving a system follow a route picked on the map, stop by stop; pick-ups on it are offered; a gravity rift on
    // it is rolled for. X or the button switches it on; the routes stay when it is switched off.
    bool waypointsAvailable() const override { return d_.system >= 0 && !shipSystems_.empty(); }
    bool waypointsOn() const override { return wp_; }
    void toggleWaypoints() override {
        if (wp_) leaveWp();
        else enterWp();
    }
    bool mapMode() const override { return wp_; }
    bool backAvailable() const override { return wp_ || RowsUI::backAvailable(); }
    std::vector<std::pair<std::string, std::string>> hints() const override {
        if (wp_ && wpFrom_ < 0) return {{"D-PAD", "Move"}, {"A", "Ships from here"}, {"B", "Back to the list"}};
        if (wp_)
            return {{"D-PAD", "Move"}, {"A", "Add stop"}, {"B", "Remove last"}, {"X", "Automatic route"}, {"START", "Done"}};
        std::vector<std::pair<std::string, std::string>> h = RowsUI::hints();
        if (waypointsAvailable()) h.push_back({"X", "Waypoints"});
        return h;
    }
    bool update(const Input& in, uint32_t now) override {
        if (!warn_.empty()) return RowsUI::update(in, now);
        if (wp_) return updateWp(in);
        if (in.pressed(Btn::X) && waypointsAvailable()) {
            enterWp();
            return false;
        }
        return RowsUI::update(in, now);
    }
    void draw(Canvas& c, uint32_t now) override {
        if (!wp_) {
            RowsUI::draw(c, now);
            return;
        }
        // Waypoint Mode: the list steps aside for the map; a banner over it, as when a system is picked on the map
        const Rect r{6, 26, 410, 0};
        std::vector<std::string> lines;
        std::string title = "Waypoint Mode";
        uint32_t warnCol = theme::red;
        std::string warn;
        if (wpFrom_ < 0) {
            lines.push_back("Choose the system your ships leave from (highlighted).");
        } else {
            const std::vector<int>& st = routes_[wpFrom_];
            title += ": ships from " + systemLabel(wpFrom_);
            std::string path = systemLabel(wpFrom_);
            for (int x : st) path += " > " + systemLabel(x);
            lines.push_back(path);
            lines.push_back(st.empty() ? "Choose the first system on the way (highlighted)."
                                       : "Choose the next system - the active one ends the route.");
            int rolls = riftRollsLeaving(g_, d_.player, wpFrom_) ? 1 : 0;
            for (int x : st) rolls += riftRollsLeaving(g_, d_.player, x) ? 1 : 0;
            if (rolls > 0)
                warn = "Leaving a gravity rift: each ship rolls " + std::to_string(rolls) + (rolls == 1 ? " die" : " dice") +
                       " - on 1-3 it is removed from the board with what it carries.";
        }
        std::vector<std::string> body, warnLines = warn.empty() ? std::vector<std::string>() : wrapText(FONT_SM, warn, r.w - 20);
        for (const std::string& l : lines)
            for (const std::string& w : wrapText(FONT_SM, l, r.w - 20)) body.push_back(w);
        // what moves (the list's summary), so the count of ships and the capacity stay in sight
        std::vector<std::string> sum = summaryCore();
        const int h = 26 + 15 * int(body.size() + warnLines.size() + sum.size()) + 8;
        c.fillRoundRect(r.x, r.y, r.w, h, 8, rgb(12, 17, 32), 225);
        c.frameRoundRect(r.x, r.y, r.w, h, 8, theme::gold, 1);
        drawText(c, FONT_MDB, r.x + 10, r.y + 5, fitText(FONT_MDB, title, r.w - 20), theme::gold);
        int y = r.y + 26;
        for (const std::string& l : body) {
            drawText(c, FONT_SM, r.x + 10, y, l, theme::text);
            y += 15;
        }
        for (const std::string& l : warnLines) {
            drawText(c, FONT_SM, r.x + 10, y, l, warnCol);
            y += 15;
        }
        for (const std::string& l : sum) {
            drawText(c, FONT_XS, r.x + 10, y + 1, fitText(FONT_XS, l, r.w - 20), theme::dim);
            y += 15;
        }
        if (!wpError_.empty()) {
            const int ey = 446 - 98;
            c.fillRoundRect(r.x, ey, r.w, 22, 6, rgb(70, 18, 18), 235);
            drawText(c, FONT_SM, r.x + 8, ey + 3, fitText(FONT_SM, wpError_, r.w - 16), rgb(255, 200, 200));
        }
    }

protected:
    MapView& map_;
    std::vector<int> shipSystems_;
    bool wp_ = false;
    int wpFrom_ = -1;
    std::vector<int> wpSources_;
    std::map<int, std::vector<int>> routes_;  // from -> stops (done or being made)
    std::string wpError_;

    std::vector<int> selectedSources() const {
        std::vector<int> out;
        for (const Row& r : rows_)
            if (!r.header && r.value > 0 && r.b == -1 && r.a != d_.system && movesAsShip(g_, d_.player, UnitType(r.c)) &&
                std::find(out.begin(), out.end(), r.a) == out.end() &&
                std::find(shipSystems_.begin(), shipSystems_.end(), r.a) != shipSystems_.end())
                out.push_back(r.a);
        return out;
    }
    void enterWp() {
        std::vector<int> src = selectedSources();
        if (src.empty()) src = shipSystems_;
        if (src.empty()) return;
        wp_ = true;
        wpError_.clear();
        wpSources_ = src;
        wpFrom_ = src.size() == 1 ? src[0] : -1;
        refreshWp(true);
    }
    void leaveWp() {
        wp_ = false;
        wpFrom_ = -1;
        wpError_.clear();
        map_.clearHighlights();
        for (auto it = routes_.begin(); it != routes_.end();) it = it->second.empty() ? routes_.erase(it) : std::next(it);
        syncMap();
        changed();
    }
    std::vector<int> nextSteps() const {
        if (wpFrom_ < 0) return wpSources_;
        auto it = routes_.find(wpFrom_);
        static const std::vector<int> none;
        return routeNextSteps(g_, d_.player, wpFrom_, it == routes_.end() ? none : it->second, d_.system);
    }
    void refreshWp(bool moveCursor) {
        const std::vector<int> next = nextSteps();
        map_.setHighlights(next);
        if (moveCursor) {
            int at = wpFrom_;
            if (wpFrom_ >= 0 && !routes_[wpFrom_].empty()) at = routes_[wpFrom_].back();
            if (at < 0 && !next.empty()) at = next[0];
            if (at >= 0) map_.setCursor(g_, at);
        }
        syncMap();
    }
    // the routes on the map: done ones end in the active system; the one being made ends where it has got to
    void syncMap() {
        std::vector<MapView::RouteLine> lines;
        for (const auto& rt : routes_) {
            const bool editing = wp_ && rt.first == wpFrom_;
            if (rt.second.empty() && !editing) continue;
            MapView::RouteLine l;
            l.from = rt.first;
            l.stops = rt.second;
            l.to = editing ? -1 : d_.system;
            l.editing = editing;
            l.color = editing ? theme::gold : theme::cyan;
            l.roll.push_back(riftRollsLeaving(g_, d_.player, rt.first) ? 1 : 0);
            for (int x : rt.second) l.roll.push_back(riftRollsLeaving(g_, d_.player, x) ? 1 : 0);
            lines.push_back(l);
        }
        map_.setRoutes(lines);
    }
    bool updateWp(const Input& in) {
        if (in.pressed(Btn::Up)) map_.moveCursor(g_, 0);
        if (in.pressed(Btn::Right)) map_.moveCursor(g_, 1);
        if (in.pressed(Btn::Down)) map_.moveCursor(g_, 2);
        if (in.pressed(Btn::Left)) map_.moveCursor(g_, 3);
        if (int z = in.zoom()) map_.setZoom(map_.zoom() + z);
        const int act = d_.system, cur = map_.cursor();
        if (in.pressed(Btn::A)) {
            wpError_.clear();
            const std::vector<int> next = nextSteps();
            if (std::find(next.begin(), next.end(), cur) == next.end()) {
                wpError_ = wpFrom_ < 0 ? "Choose one of the highlighted systems - your ships move from there."
                                       : "That system isn't a step on from the end of the route (the next steps are highlighted).";
                return false;
            }
            if (wpFrom_ < 0) {
                wpFrom_ = cur;
                refreshWp(true);
                return false;
            }
            if (cur == act) {  // the route is complete
                finishRoute();
                return false;
            }
            routes_[wpFrom_].push_back(cur);
            addPickupRows(cur);
            refreshWp(false);
            changed();
            return false;
        }
        if (in.pressed(Btn::B)) {
            wpError_.clear();
            if (wpFrom_ >= 0 && !routes_[wpFrom_].empty()) {
                routes_[wpFrom_].pop_back();
                refreshWp(true);
                changed();
            } else if (wpFrom_ >= 0 && wpSources_.size() > 1) {
                routes_.erase(wpFrom_);
                wpFrom_ = -1;
                refreshWp(true);
                changed();
            } else {
                if (wpFrom_ >= 0) routes_.erase(wpFrom_);
                leaveWp();
            }
            return false;
        }
        if (in.pressed(Btn::X)) {  // the automatic route for these ships
            if (wpFrom_ >= 0) routes_.erase(wpFrom_);
            leaveWp();
            return false;
        }
        if (in.pressed(Btn::Start)) {
            wpError_.clear();
            if (wpFrom_ < 0 || routes_[wpFrom_].empty()) {
                if (wpFrom_ >= 0) routes_.erase(wpFrom_);
                leaveWp();
                return false;
            }
            const std::vector<int> next = nextSteps();
            if (std::find(next.begin(), next.end(), act) == next.end()) {
                wpError_ = "The route doesn't reach the active system yet - keep adding stops.";
                return false;
            }
            finishRoute();
        }
        return false;
    }
    void finishRoute() {
        const int from = wpFrom_;
        if (routes_[from].empty()) routes_.erase(from);  // (straight in: the automatic route)
        wp_ = false;
        wpFrom_ = -1;
        map_.clearHighlights();
        syncMap();
        // the list again, on the ships of that system
        for (int i = 0; i < int(rows_.size()); ++i)
            if (!rows_[size_t(i)].header && rows_[size_t(i)].a == from && rows_[size_t(i)].b == -1) {
                cursor_ = i;
                break;
            }
        changed();
    }
    // a stop with our fighters / ground forces: rows to pick them up (if the list doesn't have them yet)
    void addPickupRows(int s) {
        const int p = d_.player;
        bool units = g_.systems[size_t(s)].space[size_t(p)].count(UnitType::Fighter) || g_.systems[size_t(s)].space[size_t(p)].groundForces();
        for (int16_t pl : g_.systems[size_t(s)].planets)
            if (g_.planets[size_t(pl)].units[size_t(p)].groundForces()) units = true;
        if (!units || g_.systems[size_t(s)].hasToken(p)) return;
        for (const Row& r : rows_)
            if (r.header && !r.info && r.a == s) return;
        std::vector<Row> keep;
        keep.swap(rows_);
        addSystemRows(s, true);
        std::vector<Row> added;
        added.swap(rows_);
        rows_.swap(keep);
        size_t at = rows_.size();
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].header && rows_[i].label.rfind("Can't reach", 0) == 0) {
                at = i;
                break;
            }
        if (int(at) <= cursor_) cursor_ += int(added.size());
        rows_.insert(rows_.begin() + long(at), added.begin(), added.end());
    }

    // 0.44.2 (tester: "2 Exotrireme IIs with move 2 ... don't show up"): ships near enough to wonder about that can't
    // reach the active system, listed at the end with the reason
    void addUnreachable() {
        const Game& g = g_;
        const int p = d_.player, act = d_.system;
        if (act < 0) return;
        const bool gd = moveBoost(g, p);  // (0.44.7: every +1 effect - Lightning Drives, Ionian Fuel Refinery... - not only Gravity Drive)
        const int n = int(g.systems.size());
        std::vector<int> plain(size_t(n), -1);  // moves away, ignoring everything in the way
        std::deque<int> q{act};
        plain[size_t(act)] = 0;
        while (!q.empty()) {
            const int u = q.front();
            q.pop_front();
            for (int v : routeNeighbors(g, p, u))
                if (plain[size_t(v)] < 0) {
                    plain[size_t(v)] = plain[size_t(u)] + 1;
                    q.push_back(v);
                }
        }
        bool header = false;
        for (int sys = 0; sys < n; ++sys) {
            if (sys == act || g.systems[size_t(sys)].isHyperlane() || plain[size_t(sys)] < 0) continue;
            const Force& f = g.systems[size_t(sys)].space[size_t(p)];
            for (int t : kShipIdx) {
                if (!f.n[t] || t == int(UnitType::Fighter)) continue;
                const UnitType ut = UnitType(t);
                const bool token = g.systems[size_t(sys)].hasToken(p) && !g.fx.dominusOrb && !(heroActive(g, p, "nomad") && flagshipIn(g, p, sys));
                if (!token && shipReach(g, p, sys, act, ut, moveBonusMax(g, p)).ok) continue;
                const int mv = moveValue(g, p, sys, ut), away = plain[size_t(sys)];
                if (mv <= 0 || away > mv + (gd ? 1 : 0) + 1) continue;  // (only the near ones)
                std::string why;
                if (token) why = "your command token is there";
                else if (anomaliesFor(g, p, sys) & kNebula) why = "starts in a nebula: move 1";
                else if (away > mv + (gd ? 1 : 0)) why = std::to_string(away) + " away, move " + std::to_string(mv) + (gd ? " (+1)" : "");
                else why = "blocked on the way (ships, anomaly)";
                if (!header) {
                    Row h;
                    h.header = true;
                    h.label = "Can't reach the active system";
                    rows_.push_back(h);
                    header = true;
                }
                Row r;
                r.header = r.info = true;
                r.label = std::string("  ") + std::to_string(f.n[t]) + " " + unitLabel(g, p, ut, f.n[t] > 1) + " in " + systemLabel(sys);
                r.sub = why;
                rows_.push_back(r);
            }
        }
    }

    void addSystemRows(int s, bool pickupOnly) {
        const int p = d_.player, act = d_.system;
        const Force& f = g_.systems[s].space[p];
        Row h;
        h.header = true;
        h.label = pickupOnly ? "Pick up in " + systemLabel(s) + " (on the way)" : "From " + systemLabel(s);
        h.a = s;  // (0.56.0: Waypoint Mode looks for the systems already listed)
        rows_.push_back(h);
        for (int t = 0; t < kUnitTypes; ++t) {
            if (!f.n[t]) continue;
            UnitType ut = UnitType(t);
            const bool shipLike = movesAsShip(g_, p, ut);  // ships and floating factories
            if (isStructure(ut) && !shipLike) continue;
            Reach rch = shipReach(g_, p, s, act, ut, moveBonusMax(g_, p));
            if (shipLike && ut != UnitType::Fighter && !rch.ok) continue;
            if (pickupOnly && shipLike && ut != UnitType::Fighter) continue;
            Row r;
            r.label = std::string("  ") + (ut == UnitType::SpaceDock ? "Floating factories" : unitLabel(g_, p, ut, true));
            r.sub = std::to_string(f.n[t]) + " here" + (rch.viaRift ? ", rift" : "") + (rch.needsBonus ? ", +" + std::to_string(std::max(1, int(rch.bonus))) + " move" : std::string());
            if (!shipLike || (ut == UnitType::Fighter && (!rch.ok || pickupOnly)))
                r.sub = std::to_string(f.n[t]) + " here, carried";
            else if (ut == UnitType::Fighter && rch.needsBonus && g_.fixes >= 7)  // (0.44.2: carried first, the +1 only if no room)
                r.sub = std::to_string(f.n[t]) + " here, carried / +1";
            r.max = f.n[t];
            r.a = s;
            r.b = -1;
            r.c = t;
            rows_.push_back(r);
        }
        const bool mini = isFaction(g_, p, "ralnel");  // Miniaturization (Thunder's Edge): structures ride along
        if (mini && !pickupOnly)
            for (UnitType ut : {UnitType::PDS, UnitType::SpaceDock}) {
                if (!f.count(ut) || movesAsShip(g_, p, ut)) continue;
                Row r;
                r.label = std::string("  ") + unitTypeName(ut, true) + " (carried)";
                r.sub = std::to_string(f.count(ut)) + " here, no capacity needed";
                r.max = f.count(ut);
                r.a = s;
                r.b = -1;
                r.c = int(ut);
                rows_.push_back(r);
            }
        for (int16_t pl : g_.systems[s].planets) {
            const Force& pf = g_.planets[pl].units[p];
            for (UnitType ut : {UnitType::Infantry, UnitType::Mech, UnitType::PDS, UnitType::SpaceDock}) {
                if (!pf.count(ut)) continue;
                if (isStructure(ut) && (!mini || pickupOnly)) continue;
                Row r;
                r.label = std::string("  ") + unitTypeName(ut, true) + " on " + kPlanets[g_.planets[pl].def].name;
                r.sub = std::to_string(pf.count(ut)) + (isStructure(ut) ? " there, no capacity needed" : " there, carried");
                r.max = pf.count(ut);
                r.a = s;
                r.b = pl;
                r.c = int(ut);
                rows_.push_back(r);
            }
        }
    }
    std::string systemLabel(int s) const {
        const SystemState& sy = g_.systems[s];
        if (sy.home >= 0) return playerName(g_, sy.home) + " home";
        const SystemDef& d = sy.d();
        return std::string(d.name) + (d.planetCount == 0 ? std::string(" (") + d.key + ")" : "");
    }
    void buildAnswer() override {
        answer_ = Answer();
        for (const Row& r : rows_)
            if (!r.header && r.value > 0) answer_.data.insert(answer_.data.end(), {r.a, r.b, r.c, r.value});
        // Waypoints: the routes of systems whose ships move (a route set before the ships are chosen waits)
        const std::vector<int> moving = selectedSources();
        for (const auto& rt : routes_) {
            if (rt.second.empty() || (wp_ && rt.first == wpFrom_)) continue;  // (the one being made isn't finished)
            if (std::find(moving.begin(), moving.end(), rt.first) == moving.end()) continue;
            for (int st : rt.second) answer_.data.insert(answer_.data.end(), {kWaypointTuple, rt.first, st, 0});
        }
    }
    std::vector<std::string> routeLines() const {
        std::vector<std::string> out;
        for (const auto& rt : routes_) {
            if (rt.second.empty()) continue;
            std::string l = "Route from " + systemLabel(rt.first) + ":";
            for (int st : rt.second) l += " " + systemLabel(st) + " >";
            l += " here";
            int rolls = riftRollsLeaving(g_, d_.player, rt.first) ? 1 : 0;
            for (int st : rt.second) rolls += riftRollsLeaving(g_, d_.player, st) ? 1 : 0;
            if (rolls) l += " (rift: " + std::to_string(rolls) + (rolls == 1 ? " roll)" : " rolls)");
            const std::vector<int> moving = selectedSources();
            if (std::find(moving.begin(), moving.end(), rt.first) == moving.end()) l += " - choose its ships";
            out.push_back(l);
        }
        return out;
    }
    std::vector<std::string> summary() override {
        std::vector<std::string> out = summaryCore();
        for (const std::string& l : routeLines()) out.push_back(l);
        return out;
    }
    std::vector<std::string> summaryCore() {
        Force arriving;
        for (const Row& r : rows_)
            if (!r.header && r.value > 0) arriving.add(UnitType(r.c), r.value);
        Force after = g_.systems[d_.system].space[d_.player];
        for (int t = 0; t < kUnitTypes; ++t) after.n[t] = uint8_t(after.n[t] + arriving.n[t]);
        const int cap = forceCapacity(g_, d_.player, after);  // ships and floating factories
        return {"Ships arriving: " + std::to_string(arriving.nonFighterShips()) + " (fleet limit " +
                    std::to_string(fleetLimit(g_, d_.player)) + ")",
                "Capacity used: " + std::to_string(after.count(UnitType::Fighter) + after.groundForces()) + " / " +
                    std::to_string(cap + dockFighterSlots(g_, d_.player, d_.system))};
    }
    bool hasSuggestion() const override { return true; }
    void suggest() override {
        Answer s = aiAnswer(g_, d_, rng_);
        routes_.clear();  // (the suggestion takes the automatic routes)
        syncMap();
        for (Row& r : rows_)
            if (!r.header) r.value = 0;
        for (size_t i = 0; i + 3 < s.data.size(); i += 4) {
            for (Row& r : rows_)
                if (!r.header && r.a == s.data[i] && r.b == s.data[i + 1] && r.c == s.data[i + 2])
                    r.value = std::min(r.max, s.data[i + 3]);
        }
    }
    std::string confirmLabel() const override { return "Move"; }
    std::string confirmWarning() const override {
        const std::string w = strandWarning(g_, d_.player, d_.system, answer_.data);
        return w.empty() ? fleetOverWarning(g_, d_.player, d_.system, answer_.data) : w;  // (0.58.5)
    }
    std::string clearLabel() const override { return "Clear"; }
    std::string emptyText() const override { return keyText("None of your ships can reach this system. Press START."); }

private:
    Rng rng_{5};
};

}  // namespace

// Research (0.28.0, owner): the "can research" technology cards; A opens a card, A again researches it.
class ResearchUI : public DecisionUI {
public:
    ResearchUI(const Game& g, const Decision& d) : DecisionUI(g, d) {
        std::vector<int> ids;
        std::vector<std::string> notes;
        for (const auto& o : d.options) {
            if (o.id < 0) {
                skip_ = true;
                continue;
            }
            ids.push_back(o.id);
            notes.push_back(o.detail);
        }
        pick_ = makeResearchPick(g, d.player, ids, notes, d_.prompt);
    }
    bool update(const Input& in, uint32_t) override {
        if (skip_ && !pick_->detailOpen() && in.pressed(Btn::X)) {
            answer_ = Answer();
            answer_.picks.push_back(-1);
            return true;
        }
        const int t = pick_->pick(in);
        if (t >= 0) {
            answer_ = Answer();
            answer_.picks.push_back(t);
            return true;
        }
        return false;
    }
    void draw(Canvas& c, uint32_t now) override { pick_->paint(c, now); }
    std::vector<std::pair<std::string, std::string>> hints() const override {
        if (pick_->detailOpen()) return {{"\xE2\x97\x80\xE2\x96\xB6", "Previous / next"}, {"A", "Research"}, {"B", "Back"}};
        std::vector<std::pair<std::string, std::string>> h = {{"D-PAD", "Select"}, {"A", "Read card"}};
        if (skip_) h.push_back({"X", "Don't research"});
        return h;
    }
    bool backAvailable() const override { return pick_->detailOpen(); }
    bool fullScreen() const override { return true; }

private:
    std::unique_ptr<ResearchPick> pick_;
    bool skip_ = false;
};

// 0.58.4 (owner: "Whenever the human player is choosing an available strategy card, a pop-up radial wheel menu
// appears. Between each spoke of the wheel holds the strategy cards. Dimmed cards are unavailable. When the player
// chooses an available card, the slice where the card resides is highlighted. The center of the wheel, which holds
// the button Confirm goes from grayed out to yellow."). The eight cards clockwise from the top (1 to 8 like a clock);
// left / right point at a card (its text in the right-hand panel), A chooses it, A again or the centre confirms.
class StrategyWheelUI : public DecisionUI {
public:
    StrategyWheelUI(const Game& g, const Decision& d) : DecisionUI(g, d) {
        for (const Option& o : d.options)
            if (o.id >= 0 && o.id < kStrategyCards && o.enabled) open_[o.id] = true;
        for (int k = 0; k < kStrategyCards; ++k)
            if (open_[k]) {
                cursor_ = k;
                break;
            }
    }
    bool bareClickConfirms() const override { return false; }
    bool stepsWithKeys() const override { return true; }
    int highlightedValue() const override { return cursor_; }
    bool backAvailable() const override { return chosen_ >= 0; }
    Rect area() const override { return Rect{6, 26, 410, 420}; }

    bool update(const Input& in, uint32_t) override {
        auto turn = [&](int dir) {
            for (int i = 1; i <= kStrategyCards; ++i) {
                const int k = ((cursor_ + dir * i) % kStrategyCards + kStrategyCards) % kStrategyCards;
                if (open_[k]) {
                    cursor_ = k;
                    return;
                }
            }
        };
        if (in.pressed(Btn::Right) || in.pressed(Btn::Down)) turn(1);
        if (in.pressed(Btn::Left) || in.pressed(Btn::Up)) turn(-1);
        if (in.pressed(Btn::B) && chosen_ >= 0) chosen_ = -1;
        if (in.pressed(Btn::A) && cursor_ >= 0 && open_[cursor_]) {
            if (chosen_ == cursor_) return confirm();
            chosen_ = cursor_;
            error_.clear();
        }
        if (in.pressed(Btn::Start)) {
            if (chosen_ >= 0) return confirm();
            setError(keyText("Choose a card first (A), then confirm."));
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        std::vector<std::pair<std::string, std::string>> h = {{"\xE2\x97\x80\xE2\x96\xB6", "Card"}};
        if (chosen_ >= 0 && chosen_ == cursor_) h.push_back({"A", "Confirm"});
        else h.push_back({"A", "Choose"});
        if (chosen_ >= 0) {
            if (chosen_ != cursor_) h.push_back({"START", "Confirm"});
            h.push_back({"B", "Unchoose"});
        }
        return h;
    }

    void draw(Canvas& c, uint32_t now) override {
        const Rect a = area();
        c.fillRoundRect(a.x, a.y, a.w, a.h, 10, rgb(8, 11, 22), 214);
        c.frameRoundRect(a.x, a.y, a.w, a.h, 10, theme::border);
        hits::addAction(a, [] {});  // (a click inside the window is not for the map)
        drawText(c, FONT_MDB, a.x + 12, a.y + 6, fitText(FONT_MDB, d_.prompt, a.w - 24), theme::text);
        int ty = a.y + 26;
        for (const std::string& l : wrapText(FONT_XS, d_.context, a.w - 24)) {
            if (ty > a.y + 40) break;
            drawText(c, FONT_XS, a.x + 12, ty, l, theme::dim);
            ty += 12;
        }
        const float cx = float(a.x + a.w / 2), cy = float(a.y + 214);
        const float R0 = 48.f, R1 = 158.f;
        const float span = 2.f * radial::kPi / float(kStrategyCards);
        c.fillCircle(cx, cy, R1 + 3.f, rgb(4, 6, 14), 200);
        for (int k = 0; k < kStrategyCards; ++k) {
            const float mid = -radial::kPi / 2.f + span * float(k);
            const uint32_t sc = strategyCardColor(k);
            uint32_t fill = rgb(14, 18, 32);
            int al = 240;
            if (!open_[k]) fill = rgb(10, 12, 20), al = 200;
            else if (k == chosen_) fill = mix(theme::panelHi, sc, 110);
            else if (k == cursor_) fill = mix(rgb(22, 28, 48), sc, 45);
            radial::slice(c, cx, cy, R0, R1, mid - span / 2.f, mid + span / 2.f, fill, al);
        }
        for (int k = 0; k < kStrategyCards; ++k) radial::spoke(c, cx, cy, R0, R1, -radial::kPi / 2.f + span * (float(k) - 0.5f), theme::border, 255);
        c.ring(cx, cy, R1, 2.f, theme::border);
        auto rim = [&](int k, uint32_t col, float rad) {
            const float mid = -radial::kPi / 2.f + span * float(k);
            for (float t = -span / 2.f; t < span / 2.f; t += 0.02f) c.fillCircle(cx + std::cos(mid + t) * R1, cy + std::sin(mid + t) * R1, rad, col);
        };
        if (cursor_ >= 0 && cursor_ != chosen_) rim(cursor_, theme::dim, 1.2f);
        if (chosen_ >= 0) rim(chosen_, theme::gold, 2.f);
        for (int k = 0; k < kStrategyCards; ++k) {
            const float mid = -radial::kPi / 2.f + span * float(k);
            const float ux = std::cos(mid), uy = std::sin(mid);
            const float ax = cx + ux * 104.f, ay = cy + uy * 104.f;
            const int w = 64, h = cards::strategyHeight(w);
            const bool open = open_[k];
            cards::strategy(c, k, int(ax) - w / 2, int(ay) - h / 2, w, open ? 255 : 70);
            // the card's name on its outer side; a taken card says by whom
            const int ty2 = uy < -0.2f ? int(ay) - h / 2 - 14 : uy > 0.2f ? int(ay) + h / 2 + 2 : int(ay) - h / 2 - 14;
            drawTextAligned(c, FONT_XS, int(ax) - 50, ty2, 100, Align::Center, strategyName(k),
                            open ? (k == cursor_ || k == chosen_ ? theme::text : rgb(196, 202, 218)) : theme::faint);
            const int owner = g_.cardOwner[k];
            if (!open && owner >= 0 && owner < g_.numPlayers) {
                const Player& op = g_.players[size_t(owner)];
                c.fillCircle(ax + float(w) / 2.f - 4.f, ay - float(h) / 2.f + 4.f, 10.f, rgb(10, 14, 26));
                c.ring(ax + float(w) / 2.f - 4.f, ay - float(h) / 2.f + 4.f, 10.f, 2.f, theme::player[op.color % 8]);
                drawFactionIcon(c, op.faction, int(ax) + w / 2 - 12, int(ay) - h / 2 - 4, 16);
            } else if (open && g_.cardTradeGoods[k] > 0) {  // trade goods waiting on it
                const float gx = ax + float(w) / 2.f - 2.f, gy = ay - float(h) / 2.f + 2.f;
                c.fillCircle(gx, gy, 10.f, rgb(232, 190, 60));
                drawTextAligned(c, FONT_XS, int(gx) - 10, int(gy) - 6, 20, Align::Center, "+" + std::to_string(g_.cardTradeGoods[k]), rgb(36, 26, 6));
            }
            if (open) {  // the mouse: point at a card; a click chooses it (and on the chosen one confirms)
                const int kk = k;
                hits::add(Rect{int(ax) - w / 2 - 4, int(ay) - h / 2 - 16, w + 8, h + 32}, [this, kk]() { cursor_ = kk; });
            }
        }
        radial::centre(c, cx, cy, 42.f, "Confirm", chosen_ >= 0 ? strategyName(chosen_) : "choose a card", chosen_ >= 0, now);
        hits::addButton(Rect{int(cx) - 40, int(cy) - 40, 80, 80}, Btn::Start);
        int y = int(cy + R1) + 10;
        if (cursor_ >= 0) {
            drawTextAligned(c, FONT_SM, a.x, y, a.w, Align::Center,
                            fitText(FONT_SM, std::to_string(cursor_ + 1) + " " + strategyName(cursor_) + (open_[cursor_] ? "" : " (taken)"), a.w - 20),
                            theme::text);
            y += 15;
        }
        if (!error_.empty()) drawTextAligned(c, FONT_XS, a.x, y, a.w, Align::Center, fitText(FONT_XS, error_, a.w - 20), theme::red);
    }

private:
    bool confirm() {
        answer_ = Answer();
        answer_.picks = {chosen_};
        return true;
    }
    bool open_[kStrategyCards] = {};
    int cursor_ = -1, chosen_ = -1;
};

// 0.43.0 (owner): joining the Thunder's Edge expedition as a rotary menu - the shrouded tile large, its six winglets
// round it; left / right (or the mouse) turns to a winglet, A takes it (then the payment asks), B = not now.
class ExpeditionUI : public DecisionUI {
public:
    ExpeditionUI(const Game& g, const Decision& d) : DecisionUI(g, d) {
        for (const auto& o : d.options) {
            if (o.id >= 0 && o.id < 6 && o.enabled) open_[o.id] = true;
            if (o.id < 0) notNow_ = true;
        }
        for (int k = 0; k < 6; ++k)
            if (open_[k]) {
                sel_ = k;
                break;
            }
    }
    bool update(const Input& in, uint32_t) override {
        auto step = [&](int dir) {
            for (int i = 1; i <= 6; ++i) {
                const int k = ((sel_ + dir * i) % 6 + 6) % 6;
                if (open_[k]) {
                    sel_ = k;
                    return;
                }
            }
        };
        if (in.pressed(Btn::Right) || in.pressed(Btn::Down)) step(1);
        if (in.pressed(Btn::Left) || in.pressed(Btn::Up)) step(-1);
        if (in.pressed(Btn::A) && sel_ >= 0 && open_[sel_]) return pickIt(sel_);
        if (in.pressed(Btn::X) && notNow_) return pickIt(-1);
        return false;
    }
    void draw(Canvas& c, uint32_t now) override {
        const Rect area{0, 24, 640, 432};
        c.fillRect(area.x, area.y, area.w, area.h, rgb(6, 8, 16), 245);
        drawText(c, FONT_LG, 14, area.y + 6, "The expedition to Thunder's Edge", rgb(230, 200, 120));
        int y = area.y + 32;
        for (const std::string& l : wrapText(FONT_XS, d_.context, 612)) {
            drawText(c, FONT_XS, 14, y, l, theme::dim);
            y += 12;
        }
        const float cx = 320.f, cy = float(area.y) + 236.f, R = 118.f;
        decor::Tile t;
        t.kind = decor::ThundersEdge;
        t.index = 7;
        decor::paint(c, t, cx, cy, R, now, true);
        uint32_t claim[6] = {};
        int fac[6] = {-1, -1, -1, -1, -1, -1};
        for (int k = 0; k < 6; ++k) {
            const int q = g_.expedition[size_t(k)];
            if (q >= 0 && q < g_.numPlayers) {
                claim[k] = theme::player[g_.players[size_t(q)].color % 8];
                fac[k] = g_.players[size_t(q)].faction;
            }
        }
        float wx[6], wy[6];
        decor::drawWinglets(c, cx, cy, R, claim, now, sel_, fac, open_, wx, wy);
        for (int k = 0; k < 6; ++k)  // the mouse: hover turns to it, a click takes it
            if (open_[k]) {
                const int kk = k;
                hits::add(Rect{int(wx[k]) - 34, int(wy[k]) - 20, 68, 40}, [this, kk]() { sel_ = kk; });  // (a click also presses A)
            }
        if (sel_ >= 0) {
            static const char* cost[6] = {"Spend 5 resources", "Spend 5 influence", "Discard 2 action cards", "Discard 1 secret objective",
                                          "Exhaust 1 planet with a technology specialty", "Spend 3 trade goods"};
            drawTextAligned(c, FONT_SMB, 0, area.y + area.h - 34, 640, Align::Center, std::string(cost[sel_]) + " to join this part?", theme::text);
        }
        if (!error_.empty()) drawTextAligned(c, FONT_SM, 0, area.y + area.h - 16, 640, Align::Center, error_, theme::red);
    }
    std::vector<std::pair<std::string, std::string>> hints() const override {
        std::vector<std::pair<std::string, std::string>> h = {{"\xE2\x97\x80\xE2\x96\xB6", "Turn"}, {"A", "Join this part"}};
        if (notNow_) h.push_back({"X", "Not now"});  // 0.43.4: B is back / undo
        return h;
    }
    bool backAvailable() const override { return false; }
    bool fullScreen() const override { return true; }
    bool stepsWithKeys() const override { return true; }

private:
    bool pickIt(int id) {
        answer_ = Answer();
        answer_.picks.push_back(id);
        return true;
    }
    bool open_[6] = {};
    bool notNow_ = false;
    int sel_ = -1;
};

std::unique_ptr<DecisionUI> makeDecisionUI(const Game& g, const Decision& d, MapView& map) {
    if (d.tag == Tag::StrategyPick && d.kind == DecisionKind::Pick && !d.options.empty())  // 0.58.4: the wheel
        return std::unique_ptr<DecisionUI>(new StrategyWheelUI(g, d));
    if (d.tag == Tag::Research && d.kind == DecisionKind::Pick && !d.onMap && !d.options.empty())
        return std::unique_ptr<DecisionUI>(new ResearchUI(g, d));
    if (d.tag == Tag::Faction && d.ability == int(Ab::Expedition) && d.kind == DecisionKind::Pick)  // 0.43.0: the winglet menu
        return std::unique_ptr<DecisionUI>(new ExpeditionUI(g, d));
    if (d.tag == Tag::GalaxyPlace) return makeGalaxyDraftUI(g, d, map);
    if (d.tag == Tag::DealResponse) return makeDealResponseUI(g, d, map);
    if (d.tag == Tag::Negotiate) return makeNegotiateUI(g, d);
    switch (d.kind) {
        case DecisionKind::Deal: return makeDealUI(g, d, map);
        case DecisionKind::Pick:
            if (d.onMap) return std::unique_ptr<DecisionUI>(new MapPickUI(g, d, map));
            return std::unique_ptr<DecisionUI>(new ListUI(g, d));
        case DecisionKind::Pay: return std::unique_ptr<DecisionUI>(new PayUI(g, d));
        case DecisionKind::Tokens: return std::unique_ptr<DecisionUI>(new TokensUI(g, d));
        case DecisionKind::Hits: return std::unique_ptr<DecisionUI>(new HitsUI(g, d));
        case DecisionKind::Land: return std::unique_ptr<DecisionUI>(new LandUI(g, d));
        case DecisionKind::Produce: return std::unique_ptr<DecisionUI>(new ProduceWheelUI(g, d));  // 0.58.4: the wheel
        case DecisionKind::Move: return std::unique_ptr<DecisionUI>(new MoveUI(g, d, map));
    }
    return std::unique_ptr<DecisionUI>(new ListUI(g, d));
}
