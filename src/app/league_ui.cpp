// League mode screens - see league_ui.h.
#include "app/league_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "engine/data.h"
#include "engine/personality.h"
#include "gfx/font.h"
#include "ui/backdrops.h"
#include "ui/emblems.h"
#include "ui/hits.h"
#include "ui/icons.h"
#include "ui/newgame_bg.h"
#include "ui/pcolors.h"
#include "ui/retro.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace leagueui {
namespace {

struct GuideBlurb {
    const char* key;
    const char* difficulty;
    const char* tags;
    const char* text;
};
#include "app/gen/faction_blurbs.inc"

int* g_target = nullptr;
void pickHit(Rect r, int i) {
    hits::add(r, [i] {
        if (g_target) *g_target = i;
    });
}
const GuideBlurb* blurbOf(int f) {
    if (f < 0) return nullptr;
    for (const GuideBlurb& b : kGuideBlurbs)
        if (!std::strcmp(b.key, ti::kFactions[f].key)) return &b;
    return nullptr;
}

constexpr uint32_t kDone = rgb(232, 182, 76), kGoing = rgb(84, 196, 232), kOpenC = rgb(92, 204, 122);

const char* archName(int a) { return a >= 0 && a < int(ti::Arch::Count) ? ti::kArchetypes[a].name : ""; }
std::string ordinal(int place) {
    static const char* k[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
    return place >= 0 && place < 8 ? k[place] : "-";
}
void heading(Canvas& c, const std::string& title, const std::string& sub) {
    drawTextAligned(c, FONT_LG, 0, 10, 640, Align::Center, title, theme::gold);
    if (!sub.empty()) drawTextAligned(c, FONT_XS, 0, 36, 640, Align::Center, sub, theme::dim);
}
void medal(Canvas& c, const LeagueDef* d, float cx, float cy, float r, RowState st, uint32_t now) {
    if (!d) return;
    const emblems::State es = st == kFinished ? emblems::kEarned : st == kLocked ? emblems::kLocked : emblems::kOpen;
    if (d->emblem && *d->emblem) {
        emblems::draw(c, d->emblem, cx, cy, r, d->color, es, now);
        return;
    }
    // the Custom League has no emblem: a plain token with a pencil-like mark
    c.fillCircle(cx, cy, r, st == kLocked ? rgb(40, 44, 58) : mix(rgb(30, 30, 50), d->color, 90));
    c.ring(cx, cy, r, std::max(1.f, r * 0.08f), st == kLocked ? rgb(80, 86, 104) : d->color);
    drawTextAligned(c, r >= 14.f ? FONT_SMB : FONT_XS, int(cx - r), int(cy - (r >= 14.f ? 8 : 6)), int(2 * r), Align::Center, "+",
                    st == kLocked ? rgb(110, 116, 134) : theme::text);
}
// a colour swatch with its finish
void swatch(Canvas& c, int colour, float cx, float cy, float r, uint32_t seed) {
    if (colour < 0) return;
    {
        pcolours::Finish fin(c, pcolours::around(cx, cy, r + 1.f, r + 1.f), colour, seed);
        c.fillCircle(cx, cy, r, pcolours::base(colour));
    }
    c.ring(cx, cy, r + 0.5f, 1.f, rgb(10, 10, 16));
}
std::string statusLine(const LadderEntry& e) {
    const LeagueDef* d = e.def;
    switch (e.state) {
    case kFinished: return "Finished";
    case kInProgress: return "Game " + std::to_string(e.game + 1) + " of " + std::to_string(d->maxGames) + " \xC2\xB7 " + std::to_string(e.points) + " / " +
                             std::to_string(d->target) + " points";
    case kOpen: return "Open - first to " + std::to_string(d->target) + " points in " + std::to_string(d->maxGames) + " games";
    default: {
        const LeagueDef* a = league::find(d->after ? d->after : "");
        return std::string("Locked - finish the ") + (a ? a->name : "league before it");
    }
    }
}
std::string rulesLine(const LeagueDef* d) {
    std::string s;
    auto add = [&](const std::string& x) { s += (s.empty() ? "" : " \xC2\xB7 ") + x; };
    if (d->factionDraft) add("Faction draft");
    else if (d->pool == league::kPoolPlayer) add("You pick your faction");
    else add("Factions dealt");
    add(d->kingslay ? "Kingslay on" : "No Kingslay");
    add(d->freeRefills ? "Free refills" : "No free refills");
    add(d->x1 ? "X-1 trades" : "No X-1");
    return s;
}
std::string modulesLine(const LeagueDef* d) {
    std::string s = "Base + Prophecy of Kings";
    if (d->modules & 1) s += ", Codex";
    if (d->modules & 2) s += ", Uncharted Space";
    if (d->modules & 4) s += ", Thunder's Edge";
    if (d->modules & 8) s += ", Discordant Stars";
    return s;
}
void factionChip(Canvas& c, int faction, int x, int y, int size) {
    c.fillCircle(float(x + size / 2), float(y + size / 2), float(size) / 2.f + 1.f, rgb(14, 18, 32));
    if (faction >= 0) drawFactionIcon(c, faction, x, y, size);
    else c.ring(float(x + size / 2), float(y + size / 2), float(size) / 2.f - 1.f, 1.f, theme::faint);
}
void button(Canvas& c, Rect b, const std::string& label, bool sel, bool big, uint32_t now) {
    if (sel) {
        const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
        c.fillRoundRect(b.x, b.y, b.w, b.h, 8, big ? theme::gold : theme::panelHi, 245);
        c.frameRoundRect(b.x, b.y, b.w, b.h, 8, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
    } else {
        c.fillRoundRect(b.x, b.y, b.w, b.h, 8, theme::panelHi, 220);
        c.frameRoundRect(b.x, b.y, b.w, b.h, 8, theme::border, 1);
    }
    const FontData& f = big ? FONT_MDB : FONT_SMB;
    drawTextAligned(c, f, b.x, b.y + (b.h - f.lineHeight) / 2, b.w, Align::Center, label, sel && big ? theme::black : theme::text);
}
// the table's order of the groups on the list (A): the main ladder, then what each of its leagues opens
int groupOf(const LeagueDef* d) {
    const std::string id = d->id;
    if (id == "bronze" || id == "silver" || id == "gold" || id == "ultimate") return 0;
    const std::string a = d->after ? d->after : "";
    return a == "bronze" ? 1 : a == "silver" ? 2 : 3;
}
const char* kGroupName[4] = {"The ladder", "After Bronze", "After Silver", "After Gold"};

}  // namespace

void setPickTarget(int* target) { g_target = target; }

void drawToast(Canvas& c, const std::string& text) {
    const int w = std::min(600, textWidth(FONT_SM, text) + 40);
    const Rect r{320 - w / 2, 396, w, 30};
    c.fillRoundRect(r.x, r.y, r.w, r.h, 10, rgb(30, 18, 24), 245);
    c.frameRoundRect(r.x, r.y, r.w, r.h, 10, theme::gold, 1);
    drawTextAligned(c, FONT_SM, r.x, r.y + 8, r.w, Align::Center, fitText(FONT_SM, text, r.w - 16), theme::text);
}

void drawBackdrop(Canvas& c, uint32_t now) {
    if (retro::on()) {
#if defined(__arm__)
        backdrops::drawRetroNewGame(c, now);
#else
        newgamebg::draw(c, now);
#endif
    } else if (!(retro::hd() && backdrops::drawVoidfallNewGame(c, now)))
        drawSpaceBackground(c, false);
    c.fillRect(0, 0, 640, 480, rgb(4, 6, 14), 110);  // (a little darker: the screens are mostly text)
}

// ---------------------------------------------------------------- the ladder, A: a list and the chosen league in detail
void drawLadder(Canvas& c, const std::vector<LadderEntry>& rows, int sel, uint32_t now) {
    int done = 0;
    for (const LadderEntry& e : rows) done += e.state == kFinished;
    heading(c, "Tournament", std::to_string(done) + " of " + std::to_string(rows.size()) +
                                 " leagues finished \xC2\xB7 finish a league to earn its emblem and its reward");
    // the list, grouped
    const Rect L{10, 50, 244, 392};
    drawPanel(c, L, theme::panel, theme::border, 10, 230);
    int y = L.y + 6;
    for (int g = 0; g < 4; ++g) {
        drawText(c, FONT_XS, L.x + 10, y, kGroupName[g], theme::faint);
        y += 12;
        for (size_t i = 0; i < rows.size(); ++i) {
            const LadderEntry& e = rows[i];
            if (groupOf(e.def) != g) continue;
            const Rect rr{L.x + 4, y, L.w - 8, 18};
            pickHit(rr, int(i));
            if (int(i) == sel) {
                c.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 5, theme::panelHi, 240);
                c.frameRoundRect(rr.x, rr.y, rr.w, rr.h, 5, theme::gold, 1);
            }
            medal(c, e.def, float(rr.x + 13), float(rr.y + 9), 7.5f, e.state, e.state == kFinished ? now : 0);
            const uint32_t tc = e.state == kLocked ? theme::faint : int(i) == sel ? theme::text : rgb(196, 202, 218);
            drawText(c, FONT_SMB, rr.x + 27, rr.y + 2, fitText(FONT_SMB, e.def->name, 140), tc);
            std::string st;
            uint32_t sc = theme::faint;
            if (e.state == kFinished) st = "\xE2\x9C\x93", sc = kDone;
            else if (e.state == kInProgress) st = std::to_string(e.game) + "/" + std::to_string(e.def->maxGames), sc = kGoing;
            else if (e.state == kOpen) st = "Open", sc = kOpenC;
            else st = "Locked";
            drawTextAligned(c, FONT_XS, rr.x, rr.y + 4, rr.w - 6, Align::Right, st, sc);
            y += 19;
        }
        y += 2;
    }
    // the chosen league
    if (sel < 0 || sel >= int(rows.size())) return;
    const LadderEntry& e = rows[size_t(sel)];
    const LeagueDef* d = e.def;
    const Rect R{262, 50, 368, 392};
    drawPanel(c, R, theme::panel, mix(theme::border, d->color, 90), 10, 235);
    c.fillRoundRect(R.x + 1, R.y + 1, R.w - 2, 70, 10, mix(theme::panel, d->color, 30), 255);
    medal(c, d, float(R.x + 40), float(R.y + 36), 28.f, e.state, now);
    drawText(c, FONT_LG, R.x + 78, R.y + 12, fitText(FONT_LG, d->name, R.w - 88), e.state == kLocked ? theme::dim : theme::text);
    drawText(c, FONT_SM, R.x + 78, R.y + 40, statusLine(e),
             e.state == kFinished ? kDone : e.state == kInProgress ? kGoing : e.state == kOpen ? kOpenC : theme::faint);
    int ty = R.y + 78;
    for (const std::string& l : wrapText(FONT_SM, d->notes, R.w - 24)) {
        if (ty > R.y + 78 + 15 * 3) break;
        drawText(c, FONT_SM, R.x + 12, ty, l, rgb(200, 206, 222));
        ty += 15;
    }
    ty = R.y + 140;
    // the table settings, two columns
    struct KV {
        const char* k;
        std::string v;
    } kv[] = {
        {"Players", std::to_string(d->players)},
        {"Victory", std::to_string(d->vp) + " VP"},
        {"Bots", league::difficultyName(d->difficulty)},
        {"Galaxy", league::galaxyName(d->galaxy)},
        {"Target", std::to_string(d->target) + " points"},
        {"Game limit", std::to_string(d->maxGames) + " games"},
    };
    for (int i = 0; i < 6; ++i) {
        const int x = R.x + 12 + (i % 2) * 178, yy = ty + (i / 2) * 17;
        drawText(c, FONT_XS, x, yy + 2, kv[i].k, theme::faint);
        drawText(c, FONT_SMB, x + 68, yy, kv[i].v, theme::text);
    }
    ty += 54;
    drawText(c, FONT_XS, R.x + 12, ty, fitText(FONT_XS, rulesLine(d), R.w - 24), theme::dim);
    drawText(c, FONT_XS, R.x + 12, ty + 13, fitText(FONT_XS, modulesLine(d), R.w - 24), theme::dim);
    ty += 32;
    // rivals
    const std::vector<const RivalDef*> rv = league::rivalsOf(d->id);
    drawText(c, FONT_XS, R.x + 12, ty, d->rivals > 0 ? "Rival commanders (" + std::to_string(d->rivals) + " at each table)" : "No named rivals",
             theme::faint);
    ty += 14;
    for (size_t i = 0; i < rv.size() && i < 6; ++i) {
        const int x = R.x + 12 + int(i % 2) * 178, yy = ty + int(i / 2) * 22;
        factionChip(c, ti::findFaction(rv[i]->faction), x, yy, 18);
        drawText(c, FONT_SMB, x + 23, yy + 2, fitText(FONT_SMB, rv[i]->name, 150), e.state == kLocked ? theme::dim : theme::text);
    }
    if (rv.size() > 6) drawText(c, FONT_XS, R.x + 12, ty + 66, "+" + std::to_string(rv.size() - 6) + " more", theme::faint);
    // the reward
    const Rect W{R.x + 8, R.y + R.h - 64, R.w - 16, 56};
    c.fillRoundRect(W.x, W.y, W.w, W.h, 8, rgb(10, 13, 26), 230);
    c.frameRoundRect(W.x, W.y, W.w, W.h, 8, mix(theme::border, theme::gold, 80), 1);
    drawText(c, FONT_XS, W.x + 10, W.y + 4, "Reward", theme::gold);
    int rx = W.x + 10;
    if (d->emblem && *d->emblem) {
        emblems::draw(c, d->emblem, float(rx + 15), float(W.y + 34), 14.f, d->color, emblems::kEarned, now);
        drawText(c, FONT_SMB, rx + 34, W.y + 22, emblems::name(d->emblem), theme::text);
        drawText(c, FONT_XS, rx + 34, W.y + 37, "on the title screen", theme::faint);
        rx += 182;
    }
    if (d->colour >= 0) {
        swatch(c, d->colour, float(rx + 13), float(W.y + 34), 11.f, 7);
        drawText(c, FONT_SMB, rx + 30, W.y + 22, std::string(pcolours::info(d->colour).name), theme::text);
        drawText(c, FONT_XS, rx + 30, W.y + 37, "faction colour", theme::faint);
    } else if (!(d->emblem && *d->emblem)) {
        drawText(c, FONT_SMB, rx, W.y + 24, d->reward, theme::text);
    }
    drawHintBar(c, {{"\xE2\x86\x91\xE2\x86\x93", "Choose"},
                    {"A", e.state == kInProgress ? "Continue" : e.state == kLocked ? "-" : e.state == kFinished ? "Play again" : "Start"},
                    {"X", "Rivals"},
                    {"Y", "Map"},
                    {"B", "Back"}});
}

// ---------------------------------------------------------------- the ladder, B: a map of the leagues
void drawLadderMap(Canvas& c, const std::vector<LadderEntry>& rows, int sel, uint32_t now) {
    int done = 0;
    for (const LadderEntry& e : rows) done += e.state == kFinished;
    heading(c, "Tournament", std::to_string(done) + " of " + std::to_string(rows.size()) + " leagues finished");
    // the main ladder across the top, each with the leagues it opens hanging below it
    const char* main[4] = {"bronze", "silver", "gold", "ultimate"};
    const int colX[4] = {86, 250, 430, 572};
    auto idx = [&](const std::string& id) {
        for (size_t i = 0; i < rows.size(); ++i)
            if (id == rows[i].def->id) return int(i);
        return -1;
    };
    struct Pos {
        int i;
        float x, y, r;
    };
    std::vector<Pos> pos;
    for (int m = 0; m < 4; ++m) {
        const int i = idx(main[m]);
        if (i >= 0) pos.push_back({i, float(colX[m]), 96.f, 26.f});
    }
    for (int m = 0; m < 3; ++m) {  // children: a column of up to 4 under the parent, a second column beside it after that
        std::vector<int> kids;
        for (size_t i = 0; i < rows.size(); ++i)
            if (groupOf(rows[i].def) == m + 1) kids.push_back(int(i));
        for (size_t k = 0; k < kids.size(); ++k) {
            const int col = int(k) / 4, row = int(k) % 4;
            const float x = float(colX[m]) + (kids.size() > 4 ? (col == 0 ? -46.f : 46.f) : 0.f);
            pos.push_back({kids[k], x, 176.f + float(row) * 62.f, 17.f});
        }
    }
    auto at = [&](int i) -> const Pos* {
        for (const Pos& p : pos)
            if (p.i == i) return &p;
        return nullptr;
    };
    // the lines: the main ladder, and each league to the one that opens it
    for (int m = 0; m + 1 < 4; ++m) {
        const Pos *a = at(idx(main[m])), *b = at(idx(main[m + 1]));
        if (!a || !b) continue;
        const bool lit = rows[size_t(a->i)].state == kFinished;
        c.line(a->x + a->r, a->y, b->x - b->r, b->y, lit ? theme::gold : theme::border, lit ? 3.f : 2.f);
    }
    for (const Pos& p : pos) {
        const LeagueDef* d = rows[size_t(p.i)].def;
        if (groupOf(d) == 0) continue;
        const Pos* par = at(idx(d->after ? d->after : ""));
        if (!par) continue;
        const bool lit = rows[size_t(par->i)].state == kFinished;
        const float midY = par->y + par->r + 24.f;
        c.line(par->x, par->y + par->r, par->x, midY, lit ? theme::goldDim : rgb(40, 50, 78), 1.5f);
        c.line(par->x, midY, p.x, midY, lit ? theme::goldDim : rgb(40, 50, 78), 1.5f);
        c.line(p.x, midY, p.x, p.y - p.r, lit ? theme::goldDim : rgb(40, 50, 78), 1.5f);
    }
    for (const Pos& p : pos) {
        const LadderEntry& e = rows[size_t(p.i)];
        if (p.i == sel) {
            const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500) / 1500.f * 6.2831853f);
            c.fillCircle(p.x, p.y, p.r + 7.f, theme::gold, int(40.f + 40.f * pulse));
            c.ring(p.x, p.y, p.r + 5.f, 2.f, theme::gold);
        }
        pickHit(Rect{int(p.x - p.r), int(p.y - p.r), int(2 * p.r), int(2 * p.r)}, p.i);
        medal(c, e.def, p.x, p.y, p.r, e.state, e.state == kFinished || p.i == sel ? now : 0);
        if (e.state == kInProgress) {  // a progress arc
            const float f = float(e.points) / float(std::max(1, e.def->target));
            for (int k = 0; k < 24; ++k) {
                if (float(k) / 24.f > f) break;
                const float a0 = -1.5708f + float(k) / 24.f * 6.2831853f, a1 = a0 + 6.2831853f / 24.f;
                c.line(p.x + std::cos(a0) * (p.r + 3.f), p.y + std::sin(a0) * (p.r + 3.f), p.x + std::cos(a1) * (p.r + 3.f),
                       p.y + std::sin(a1) * (p.r + 3.f), kGoing, 2.f);
            }
        }
        const FontData& f = groupOf(e.def) == 0 ? FONT_SMB : FONT_XS;
        drawTextAligned(c, f, int(p.x) - 60, int(p.y + p.r + (groupOf(e.def) == 0 ? 3 : 2)), 120, Align::Center,
                        fitText(f, e.def->name, 118), e.state == kLocked ? theme::faint : p.i == sel ? theme::gold : theme::text);
    }
    // the chosen one, along the bottom
    if (sel >= 0 && sel < int(rows.size())) {
        const LadderEntry& e = rows[size_t(sel)];
        const LeagueDef* d = e.def;
        const Rect B{10, 404, 620, 40};
        drawPanel(c, B, theme::panel, mix(theme::border, d->color, 90), 8, 235);
        drawText(c, FONT_MDB, B.x + 10, B.y + 3, d->name, theme::text);
        drawText(c, FONT_XS, B.x + 10, B.y + 22, statusLine(e) + " \xC2\xB7 " + std::to_string(d->players) + " players \xC2\xB7 " +
                                                    league::difficultyName(d->difficulty) + " bots",
                 theme::dim);
        int rx = B.x + B.w - 10;
        if (d->colour >= 0) {
            const std::string n = pcolours::info(d->colour).name;
            rx -= textWidth(FONT_SMB, n);
            drawText(c, FONT_SMB, rx, B.y + 12, n, theme::text);
            swatch(c, d->colour, float(rx - 13), float(B.y + 20), 9.f, 3);
            rx -= 34;
        }
        if (d->emblem && *d->emblem) {
            const std::string n = emblems::name(d->emblem);
            rx -= textWidth(FONT_SMB, n);
            drawText(c, FONT_SMB, rx, B.y + 12, n, theme::text);
            emblems::draw(c, d->emblem, float(rx - 14), float(B.y + 20), 10.f, d->color, emblems::kEarned, now);
        }
    }
    drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Open"}, {"X", "Rivals"}, {"Y", "List"}, {"B", "Back"}});
}

// ---------------------------------------------------------------- a league in progress
void drawHub(Canvas& c, const HubView& v, uint32_t now) {
    const LeagueDef* d = v.def;
    if (!d) return;
    medal(c, d, 34.f, 34.f, 22.f, kInProgress, now);
    drawText(c, FONT_LG, 64, 10, d->name, theme::text);
    drawText(c, FONT_SM, 64, 38,
             "Game " + std::to_string(v.game + 1) + " of " + std::to_string(d->maxGames) + " \xC2\xB7 first to " + std::to_string(d->target) +
                 " points wins the league",
             theme::dim);
    // standings
    const Rect S{10, 62, 360, 300};
    drawPanel(c, S, theme::panel, theme::border, 10, 235);
    drawText(c, FONT_SMB, S.x + 10, S.y + 6, "Standings", theme::gold);
    drawTextAligned(c, FONT_XS, S.x, S.y + 9, S.w - 12, Align::Right, "last game", theme::faint);
    std::vector<Standing> t = v.table;
    std::stable_sort(t.begin(), t.end(), [](const Standing& a, const Standing& b) { return a.points > b.points; });
    int y = S.y + 28;
    const int rowH = std::min(44, (S.h - 34) / std::max<int>(1, int(t.size())));
    const bool compact = rowH < 34;  // (a league with many rivals: one line each)
    for (size_t i = 0; i < t.size(); ++i) {
        const Standing& s = t[i];
        if (s.you) c.fillRoundRect(S.x + 4, y - 2, S.w - 8, rowH - 2, 6, mix(theme::panelHi, theme::gold, 30), 240);
        if (compact) {
            drawText(c, FONT_SMB, S.x + 10, y + 1, std::to_string(i + 1), s.you ? theme::gold : theme::dim);
            factionChip(c, s.faction, S.x + 30, y, 16);
            drawText(c, FONT_SMB, S.x + 52, y + 1, fitText(FONT_SMB, s.name, 150), s.you ? theme::gold : theme::text);
            drawText(c, FONT_XS, S.x + 206, y + 3, fitText(FONT_XS, s.you ? "" : archName(s.arch), 60), theme::faint);
            const int bx = S.x + 52, bw = 196, by = y + rowH - 6;
            c.fillRoundRect(bx, by, bw, 3, 1, rgb(30, 36, 56));
            c.fillRoundRect(bx, by, std::max(2, bw * std::min(s.points, d->target) / std::max(1, d->target)), 3, 1, s.you ? theme::gold : d->color);
            drawTextAligned(c, FONT_SMB, S.x + 250, y + 1, 48, Align::Right, std::to_string(s.points), s.you ? theme::gold : theme::text);
            if (s.lastPlace >= 0)
                drawTextAligned(c, FONT_XS, S.x + 300, y + 3, S.w - 326, Align::Right, ordinal(s.lastPlace), theme::dim);
            if (s.move != 0)
                drawText(c, FONT_XS, S.x + S.w - 22, y + 3, s.move > 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC", s.move > 0 ? kOpenC : theme::red);
            y += rowH;
            continue;
        }
        drawText(c, FONT_MDB, S.x + 10, y + 4, std::to_string(i + 1), s.you ? theme::gold : theme::dim);
        factionChip(c, s.faction, S.x + 30, y + 1, 22);
        drawText(c, FONT_SMB, S.x + 58, y, fitText(FONT_SMB, s.name, 190), s.you ? theme::gold : theme::text);
        drawText(c, FONT_XS, S.x + 58, y + 15,
                 fitText(FONT_XS, std::string(s.faction >= 0 ? ti::factionShortName(s.faction) : "faction not chosen") + (s.you ? "" : std::string(" \xC2\xB7 ") + archName(s.arch)), 190),
                 theme::faint);
        // progress toward the target
        const int bx = S.x + 58, bw = 190, by = y + rowH - 9;
        c.fillRoundRect(bx, by, bw, 4, 2, rgb(30, 36, 56));
        c.fillRoundRect(bx, by, std::max(2, bw * std::min(s.points, d->target) / std::max(1, d->target)), 4, 2, s.you ? theme::gold : d->color);
        drawTextAligned(c, FONT_LG, S.x + 250, y + 2, 48, Align::Right, std::to_string(s.points), s.you ? theme::gold : theme::text);
        if (s.lastPlace >= 0) drawTextAligned(c, FONT_XS, S.x + 300, y + 4, S.w - 312, Align::Right, ordinal(s.lastPlace), theme::dim);
        if (s.move != 0)
            drawTextAligned(c, FONT_XS, S.x + 300, y + 16, S.w - 312, Align::Right, s.move > 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC",
                            s.move > 0 ? kOpenC : theme::red);
        y += rowH;
    }
    // the next game
    const Rect N{378, 62, 252, 150};
    drawPanel(c, N, theme::panel, mix(theme::border, d->color, 90), 10, 235);
    drawText(c, FONT_SMB, N.x + 10, N.y + 6, "Next: game " + std::to_string(v.game + 1), theme::gold);
    const std::string lines[] = {
        std::to_string(d->players) + " players \xC2\xB7 " + std::to_string(d->vp) + " VP \xC2\xB7 " + league::difficultyName(d->difficulty) + " bots",
        v.galaxy >= 0 ? std::string("Galaxy: ") + (v.galaxy ? "Draft" : "Random") + keyText(" (X: change)")
                      : std::string("Galaxy: ") + league::galaxyName(d->galaxy),
        d->factionDraft ? "Faction draft: a random seat picks first" : d->pool == league::kPoolPlayer ? "You pick your faction" : "Factions dealt at random",
        rulesLine(d).substr(rulesLine(d).find("\xC2\xB7") + 3),
    };
    int ly = N.y + 26;
    for (const std::string& l : lines) {
        drawText(c, FONT_XS, N.x + 10, ly, fitText(FONT_XS, l, N.w - 20), theme::dim);
        ly += 14;
    }
    if (!v.yourRival.empty()) {
        c.hline(N.x + 10, N.x + N.w - 10, ly + 4, theme::border);
        drawText(c, FONT_XS, N.x + 10, ly + 10, "Your rival this game", theme::faint);
        drawText(c, FONT_SMB, N.x + 10, ly + 23, fitText(FONT_SMB, v.yourRival, N.w - 20), theme::text);
        drawText(c, FONT_XS, N.x + 10, ly + 39,
                 v.rivalGap != 0 ? std::to_string(std::abs(v.rivalGap)) + (std::abs(v.rivalGap) == 1 ? " point " : " points ") +
                                       (v.rivalGap > 0 ? "ahead of you" : "behind you") + " - finish above them for +1"
                                 : "level with you - finish above them for +1",
                 kGoing);
    }
    // a rival's word
    if (!v.quote.empty()) {
        const Rect Q{378, 220, 252, 142};
        drawPanel(c, Q, rgb(12, 16, 30), theme::border, 10, 235);
        factionChip(c, v.talkerFaction, Q.x + 10, Q.y + 10, 30);
        drawText(c, FONT_SMB, Q.x + 48, Q.y + 10, fitText(FONT_SMB, v.talker, Q.w - 56), theme::gold);
        drawText(c, FONT_XS, Q.x + 48, Q.y + 26, "before the game", theme::faint);
        int qy = Q.y + 48;
        for (const std::string& l : wrapText(FONT_SM, "\"" + v.quote + "\"", Q.w - 24)) {
            if (qy > Q.y + Q.h - 16) break;
            drawText(c, FONT_SM, Q.x + 12, qy, l, rgb(214, 218, 232));
            qy += 16;
        }
    }
    // the buttons
    const char* labels[4] = {v.saved ? "Continue game" : v.pickFirst ? "Pick your faction" : "Play game", "Rivals", "League rules", "Give up"};
    const Rect bs[4] = {{10, 372, 250, 40}, {270, 372, 116, 40}, {394, 372, 116, 40}, {518, 372, 112, 40}};
    for (int i = 0; i < 4; ++i) {
        std::string l = labels[i];
        if (i == 0 && !v.pickFirst) l += " " + std::to_string(v.game + 1);
        button(c, bs[i], l, v.sel == i, i == 0, now);
        pickHit(bs[i], i);
    }
    drawTextAligned(c, FONT_XS, 0, 418, 640, Align::Center,
                    "Points: 1st " + std::to_string(league::placePoints(0)) + ", 2nd " + std::to_string(league::placePoints(1)) + ", 3rd " +
                        std::to_string(league::placePoints(2)) + ", 4th " + std::to_string(league::placePoints(3)) + " \xC2\xB7 +1 for each bonus",
                    theme::faint);
    if (v.galaxy >= 0) drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Choose"}, {"A", "Open"}, {"X", "Galaxy"}, {"B", "Tournament"}});
    else drawHintBar(c, {{"\xE2\x97\x80\xE2\x96\xB6", "Choose"}, {"A", "Open"}, {"B", "Tournament"}});
}

// ---------------------------------------------------------------- the faction draft
void drawDraft(Canvas& c, const DraftView& v, uint32_t now) {
    const LeagueDef* d = v.def;
    if (!d) return;
    if (v.pickOnly)
        heading(c, "Your faction", std::string(d->name) + " \xC2\xB7 you keep this faction for the whole league; your rivals keep theirs");
    else
        heading(c, "Faction draft", std::string(d->name) + " \xC2\xB7 game " + std::to_string(v.game + 1) +
                                        " \xC2\xB7 a random seat picks first, then round the table; the last to pick is the speaker");
    // the picking order
    const int n = int(v.order.size());
    const int cw = std::min(100, (620 - (n - 1) * 4) / std::max(1, n));
    const int top = n ? 120 : 52;  // (picking your league faction: no picking order)
    int x = 320 - (n * cw + (n - 1) * 4) / 2;
    for (int i = 0; i < n; ++i) {
        const DraftSeat& s = v.order[size_t(i)];
        const Rect r{x, 52, cw, 58};
        const bool now_ = i == v.current;
        c.fillRoundRect(r.x, r.y, r.w, r.h, 8, now_ ? mix(theme::panelHi, theme::gold, 40) : theme::panel, 235);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 8, now_ ? theme::gold : s.you ? theme::goldDim : theme::border, now_ ? 2 : 1);
        drawText(c, FONT_XS, r.x + 6, r.y + 4, std::to_string(i + 1) + (i == n - 1 ? (cw < 80 ? "  spkr" : "  speaker") : ""), i == n - 1 ? theme::gold : theme::faint);
        factionChip(c, s.faction, r.x + r.w - 26, r.y + 4, 20);
        drawText(c, FONT_SMB, r.x + 6, r.y + 22, fitText(FONT_SMB, s.you ? "You" : s.name, r.w - 10), s.you ? theme::gold : theme::text);
        drawText(c, FONT_XS, r.x + 6, r.y + 39,
                 fitText(FONT_XS, s.faction >= 0 ? ti::factionShortName(s.faction) : now_ ? "picking..." : "waiting", r.w - 10),
                 s.faction >= 0 ? theme::dim : now_ ? theme::gold : theme::faint);
        if (i + 1 < n) drawText(c, FONT_XS, r.x + r.w - 1, r.y + 24, "\xE2\x96\xB8", theme::faint);
        x += cw + 4;
    }
    // the factions on offer
    const Rect G{10, top, 380, 436 - top};
    drawPanel(c, G, theme::panel, theme::border, 10, 235);
    drawText(c, FONT_SMB, G.x + 10, G.y + 6, "On offer (" + std::to_string(v.offer.size()) + ")", theme::gold);
    drawTextAligned(c, FONT_XS, G.x, G.y + 9, G.w - 12, Align::Right, v.pickOnly && v.offer.size() > 12 ? "every faction this league deals" : "players + 3, favoured factions more often", theme::faint);
    // the grid: 3 across up to 9 factions, then 4 and 5 across; rows past the panel scroll with the highlight
    const int cnt = int(v.offer.size());
    const int cols = cnt <= 9 ? 3 : cnt <= 16 ? 4 : 5, rowsN = (cnt + cols - 1) / cols;
    const int cardW = (G.w - 20 - (cols - 1) * 6) / cols, availH = G.h - 34;
    int cardH = std::min(90, availH / std::max(1, rowsN) - 6), firstRow = 0;
    if (cardH < 60) {
        cardH = 60;
        const int vis = std::max(1, availH / (cardH + 6));
        firstRow = std::max(0, std::min(rowsN - vis, std::max(0, v.sel) / cols - vis / 2));
        if (firstRow > 0) drawTextAligned(c, FONT_XS, G.x, G.y + 20, G.w, Align::Center, "\xE2\x96\xB2", theme::faint);
        if (firstRow + vis < rowsN) drawTextAligned(c, FONT_XS, G.x, G.y + G.h - 12, G.w, Align::Center, "\xE2\x96\xBC", theme::faint);
    }
    const int icon = std::min(44, cardH - 46);
    for (size_t k = 0; k < v.offer.size(); ++k) {
        const int f = v.offer[k];
        const int row = int(k) / cols - firstRow;
        if (row < 0 || G.y + 28 + row * (cardH + 6) + cardH > G.y + G.h - 4) continue;
        const Rect r{G.x + 10 + int(k % cols) * (cardW + 6), G.y + 28 + row * (cardH + 6), cardW, cardH};
        int taker = -1;
        for (int i = 0; i < n; ++i)
            if (v.order[size_t(i)].faction == f) taker = i;
        const bool sel = int(k) == v.sel && taker < 0;
        c.fillRoundRect(r.x, r.y, r.w, r.h, 8, sel ? theme::panelHi : rgb(14, 19, 34), 240);
        c.frameRoundRect(r.x, r.y, r.w, r.h, 8, sel ? theme::gold : theme::border, sel ? 2 : 1);
        pickHit(r, int(k));
        drawFactionIcon(c, f, r.x + (r.w - icon) / 2, r.y + 6, icon, taker >= 0 ? 60 : 255);
        drawTextAligned(c, FONT_SMB, r.x, r.y + icon + 10, r.w, Align::Center, fitText(FONT_SMB, ti::factionShortName(f), r.w - 6),
                        taker >= 0 ? theme::faint : theme::text);
        const GuideBlurb* b = blurbOf(f);
        if (taker >= 0)
            drawTextAligned(c, FONT_XS, r.x, r.y + icon + 27, r.w, Align::Center,
                            fitText(FONT_XS, "taken by " + (v.order[size_t(taker)].you ? std::string("you") : v.order[size_t(taker)].name), r.w - 6),
                            theme::faint);
        else if (b) {
            const bool fav = league::listHas(d->favoured, ti::kFactions[f].key);
            drawTextAligned(c, FONT_XS, r.x, r.y + icon + 27, r.w, Align::Center, fav ? std::string("\xE2\x98\x85 favoured") : std::string(b->difficulty),
                            fav ? theme::gold : theme::faint);
        }
    }
    // the highlighted one
    const Rect I{398, top, 232, 436 - top};
    drawPanel(c, I, theme::panel, theme::border, 10, 235);
    if (v.sel >= 0 && v.sel < int(v.offer.size())) {
        const int f = v.offer[size_t(v.sel)];
        drawFactionIcon(c, f, I.x + 10, I.y + 10, 40);
        drawText(c, FONT_MDB, I.x + 56, I.y + 10, fitText(FONT_MDB, ti::kFactions[f].name, I.w - 62), theme::text);
        if (const GuideBlurb* b = blurbOf(f)) {
            drawText(c, FONT_XS, I.x + 56, I.y + 30, b->difficulty, theme::gold);
            int yy = I.y + 58;
            for (const std::string& l : wrapText(FONT_XS, b->tags, I.w - 20)) {
                drawText(c, FONT_XS, I.x + 10, yy, l, kGoing);
                yy += 13;
            }
            yy += 4;
            for (const std::string& l : wrapText(FONT_SM, b->text, I.w - 20)) {
                if (yy > I.y + I.h - 18) break;
                drawText(c, FONT_SM, I.x + 10, yy, l, rgb(200, 206, 222));
                yy += 15;
            }
        }
    }
    const bool yours = !v.done && v.current >= 0 && v.current < n && v.order[size_t(v.current)].you;
    if (v.done) drawHintBar(c, {{"A", v.pickOnly ? "Keep it - play the first game" : "Start the game"}, {"B", "Back"}});
    else if (yours || v.pickOnly) drawHintBar(c, {{"D-PAD", "Choose"}, {"A", "Take it"}, {"B", "Back"}});
    else drawHintBar(c, {{"A", "Hurry"}, {"B", "Back"}});
}

// ---------------------------------------------------------------- after a game
void drawResults(Canvas& c, const ResultView& v, uint32_t now) {
    const LeagueDef* d = v.def;
    if (!d) return;
    (void)now;
    heading(c, "Game " + std::to_string(v.game) + " results", std::string(d->name) + " \xC2\xB7 " + std::to_string(d->maxGames - v.game) +
                                                                     " games left \xC2\xB7 first to " + std::to_string(d->target) + " points");
    const Rect T{10, 50, 620, 30 + 26 * int(v.rows.size())};
    drawPanel(c, T, theme::panel, theme::border, 10, 235);
    const int cx[] = {T.x + 10, T.x + 54, T.x + 300, T.x + 360, T.x + 420, T.x + 486, T.x + 556};
    const char* hd[] = {"", "Commander", "VP", "Place", "Bonus", "Gained", "Total"};
    for (int i = 1; i < 7; ++i)
        drawTextAligned(c, FONT_XS, i == 1 ? cx[i] : cx[i] - 30, T.y + 8, i == 1 ? 200 : 60, i == 1 ? Align::Left : Align::Right, hd[i], theme::faint);
    int y = T.y + 26;
    for (const ResultRow& r : v.rows) {
        if (r.you) c.fillRoundRect(T.x + 4, y - 2, T.w - 8, 24, 5, mix(theme::panelHi, theme::gold, 30), 240);
        drawText(c, FONT_MDB, cx[0], y + 2, ordinal(r.place), r.place == 0 ? theme::gold : theme::dim);
        factionChip(c, r.faction, cx[1], y, 20);
        drawText(c, FONT_SMB, cx[1] + 26, y + 3, fitText(FONT_SMB, r.you ? "You" : r.name, 200), r.you ? theme::gold : r.named ? theme::text : theme::dim);
        drawTextAligned(c, FONT_SMB, cx[2] - 30, y + 3, 60, Align::Right, std::to_string(r.vp), theme::text);
        if (r.named) {
            drawTextAligned(c, FONT_SMB, cx[3] - 30, y + 3, 60, Align::Right, "+" + std::to_string(r.placePts), theme::text);
            drawTextAligned(c, FONT_SMB, cx[4] - 30, y + 3, 60, Align::Right, r.bonusPts ? "+" + std::to_string(r.bonusPts) : "-",
                            r.bonusPts ? kOpenC : theme::faint);
            drawTextAligned(c, FONT_SMB, cx[5] - 30, y + 3, 60, Align::Right, "+" + std::to_string(r.placePts + r.bonusPts), theme::gold);
            drawTextAligned(c, FONT_MDB, cx[6] - 40, y + 1, 70, Align::Right, std::to_string(r.total), r.you ? theme::gold : theme::text);
            if (r.move) drawText(c, FONT_XS, cx[6] + 34, y + 5, r.move > 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC", r.move > 0 ? kOpenC : theme::red);
        } else
            drawTextAligned(c, FONT_XS, cx[3] - 30, y + 5, cx[6] - cx[3] + 40, Align::Center, "not in the standings", theme::faint);
        y += 26;
    }
    // your bonuses
    const int by = T.y + T.h + 8;
    const Rect B{10, by, 300, 436 - by};
    drawPanel(c, B, theme::panel, theme::border, 10, 235);
    drawText(c, FONT_SMB, B.x + 10, B.y + 6, "Your bonuses", theme::gold);
    int yy = B.y + 26;
    for (const auto& b : v.yourBonuses) {
        if (yy > B.y + B.h - 14) break;
        drawText(c, FONT_SMB, B.x + 12, yy - 1, b.second ? "\xE2\x9C\x93" : "\xC2\xB7", b.second ? kOpenC : theme::faint);
        drawText(c, FONT_XS, B.x + 28, yy, fitText(FONT_XS, b.first, B.w - 70), b.second ? theme::text : theme::faint);
        if (b.second) drawTextAligned(c, FONT_XS, B.x, yy, B.w - 10, Align::Right, "+1", kOpenC);
        yy += 14;
    }
    // a rival speaks
    const Rect Q{318, by, 312, 436 - by};
    drawPanel(c, Q, rgb(12, 16, 30), theme::border, 10, 235);
    factionChip(c, v.speakerFaction, Q.x + 10, Q.y + 10, 30);
    drawText(c, FONT_SMB, Q.x + 48, Q.y + 10, fitText(FONT_SMB, v.speaker, Q.w - 56), theme::gold);
    int qy = Q.y + 34;
    for (const std::string& l : wrapText(FONT_SM, "\"" + v.quote + "\"", Q.w - 60)) {
        drawText(c, FONT_SM, Q.x + 48, qy, l, rgb(214, 218, 232));
        qy += 16;
    }
    if (!v.memory.empty()) {
        qy += 4;
        for (const std::string& l : wrapText(FONT_XS, v.memory, Q.w - 20)) {
            if (qy > Q.y + Q.h - 14) break;
            drawText(c, FONT_XS, Q.x + 10, qy, l, rgb(232, 140, 120));
            qy += 13;
        }
    }
    drawHintBar(c, {{"A", "Continue"}});
}

// ---------------------------------------------------------------- a league finished (or lost)
void drawWon(Canvas& c, const WonView& v, uint32_t now) {
    const LeagueDef* d = v.def;
    if (!d) return;
    const float cx = 320.f, cy = 150.f;
    if (v.won) {  // rays behind the medal
        const float spin = float(now % 12000) / 12000.f * 6.2831853f;
        for (int i = 0; i < 16; ++i) {
            const float a = spin + float(i) / 16.f * 6.2831853f, b = a + 0.12f;
            float xs[3] = {cx, cx + std::cos(a) * 150.f, cx + std::cos(b) * 150.f};
            float ys[3] = {cy, cy + std::sin(a) * 150.f, cy + std::sin(b) * 150.f};
            c.fillPolygon(xs, ys, 3, d->color, 26);
        }
        c.fillCircle(cx, cy, 74.f, d->color, 40);
    }
    medal(c, d, cx, cy, 62.f, v.won ? kFinished : kOpen, now);
    drawTextAligned(c, FONT_XL, 0, 222, 640, Align::Center, v.won ? std::string(d->name) + " won!" : std::string(d->name) + " lost",
                    v.won ? theme::gold : theme::red);
    drawTextAligned(c, FONT_SM, 0, 258, 640, Align::Center,
                    v.won ? "You finished first with " + std::to_string(v.points) + " points in " + std::to_string(v.games) + " games."
                          : v.winner + " finished first. Start the league again from 0 points when you are ready.",
                    theme::text);
    if (!v.won) {
        if (!v.table.empty()) {
            std::vector<Standing> t = v.table;
            std::stable_sort(t.begin(), t.end(), [](const Standing& a, const Standing& b) { return a.points > b.points; });
            const Rect R{170, 284, 300, 22 + 22 * int(t.size())};
            drawPanel(c, R, theme::panel, theme::border, 10, 235);
            drawText(c, FONT_XS, R.x + 10, R.y + 5, "Final standings", theme::faint);
            int y = R.y + 22;
            for (size_t i = 0; i < t.size(); ++i) {
                drawText(c, FONT_SMB, R.x + 10, y, std::to_string(i + 1), t[i].you ? theme::gold : theme::dim);
                factionChip(c, t[i].faction, R.x + 28, y - 1, 18);
                drawText(c, FONT_SMB, R.x + 52, y, fitText(FONT_SMB, t[i].you ? "You" : t[i].name, 190), t[i].you ? theme::gold : theme::text);
                drawTextAligned(c, FONT_SMB, R.x, y, R.w - 12, Align::Right, std::to_string(t[i].points), t[i].you ? theme::gold : theme::text);
                y += 22;
            }
        }
        drawHintBar(c, {{"A", "Back to the Tournament"}});
        return;
    }
    const Rect R{60, 280, 520, 158};
    drawPanel(c, R, theme::panel, mix(theme::border, theme::gold, 100), 10, 235);
    drawText(c, FONT_SMB, R.x + 12, R.y + 8, "Unlocked", theme::gold);
    int x = R.x + 16;
    if (d->emblem && *d->emblem) {
        emblems::draw(c, d->emblem, float(x + 20), float(R.y + 50), 18.f, d->color, emblems::kEarned, now);
        drawText(c, FONT_SMB, x + 46, R.y + 36, emblems::name(d->emblem), theme::text);
        drawText(c, FONT_XS, x + 46, R.y + 52, "now under the title menu", theme::faint);
        x += 250;
    }
    if (d->colour >= 0) {
        swatch(c, d->colour, float(x + 18), float(R.y + 50), 16.f, 11);
        drawText(c, FONT_SMB, x + 42, R.y + 36, std::string(pcolours::info(d->colour).name) + " colour", theme::text);
        drawText(c, FONT_XS, x + 42, R.y + 52, "in New Game > Colour", theme::faint);
    }
    if (!v.opens.empty()) {
        drawText(c, FONT_XS, R.x + 12, R.y + 80, "New leagues open", theme::faint);
        int lx = R.x + 12, ly = R.y + 94;
        for (const LeagueDef* o : v.opens) {
            const int w = textWidth(FONT_SM, o->name) + 30;
            if (lx + w > R.x + R.w - 10) lx = R.x + 12, ly += 20;
            c.fillRoundRect(lx, ly, w, 18, 9, mix(theme::panelHi, o->color, 50), 240);
            medal(c, o, float(lx + 10), float(ly + 9), 6.5f, kOpen, 0);
            drawText(c, FONT_SM, lx + 21, ly + 2, o->name, theme::text);
            lx += w + 5;
        }
    }
    drawHintBar(c, {{"A", "Back to the Tournament"}});
}

// ---------------------------------------------------------------- the rivals
void drawRivals(Canvas& c, const LeagueDef* lg, const std::vector<RivalCard>& cards, int sel, uint32_t now) {
    (void)now;
    heading(c, "Rival commanders", lg ? std::string(lg->name) + " \xC2\xB7 they remember every game they played with you" : "");
    const int cols = 2, cw = 306, ch = 124;
    const size_t first = size_t(std::max(0, sel) / 6 * 6);  // six to a page
    if (cards.size() > 6)
        drawTextAligned(c, FONT_XS, 0, 36, 630, Align::Right,
                        "page " + std::to_string(first / 6 + 1) + " of " + std::to_string((cards.size() + 5) / 6), theme::faint);
    for (size_t i = first; i < cards.size() && i < first + 6; ++i) {
        const RivalCard& k = cards[i];
        const RivalDef* r = k.def;
        const size_t at = i - first;
        const Rect R{10 + int(at % cols) * (cw + 8), 52 + int(at / cols) * (ch + 6), cw, ch};
        const bool s = int(i) == sel;
        pickHit(R, int(i));
        drawPanel(c, R, s ? theme::panelHi : theme::panel, s ? theme::gold : theme::border, 10, 235);
        const int f = ti::findFaction(r->faction);
        factionChip(c, f, R.x + 10, R.y + 10, 34);
        drawText(c, FONT_MDB, R.x + 52, R.y + 8, fitText(FONT_MDB, r->name, R.w - 60), theme::text);
        drawText(c, FONT_XS, R.x + 52, R.y + 28, fitText(FONT_XS, std::string(f >= 0 ? ti::factionShortName(f) : "?") + " \xC2\xB7 " + archName(r->arch), R.w - 60),
                 theme::gold);
        int yy = R.y + 48;
        for (const std::string& l : wrapText(FONT_XS, std::string("Grudge: ") + r->grudge, R.w - 20)) {
            if (yy > R.y + 74) break;
            drawText(c, FONT_XS, R.x + 10, yy, l, rgb(232, 150, 130));
            yy += 13;
        }
        for (const std::string& l : wrapText(FONT_XS, std::string("\"") + r->taunt + "\"", R.w - 20)) {
            if (yy > R.y + 90) break;
            drawText(c, FONT_XS, R.x + 10, yy, l, rgb(200, 206, 222));
            yy += 13;
        }
        c.hline(R.x + 10, R.x + R.w - 10, R.y + ch - 22, theme::border);
        drawText(c, FONT_XS, R.x + 10, R.y + ch - 17,
                 fitText(FONT_XS, k.games ? std::to_string(k.games) + " games together: you ahead " + std::to_string(k.youAbove) + ", them ahead " +
                                                std::to_string(k.theyAbove) + ", promises broken " + std::to_string(k.broken)
                                          : "Never met",
                         R.w - 20),
                 theme::dim);
    }
    drawHintBar(c, {{"D-PAD", "Choose"}, {"B", "Back"}});
}

// ---------------------------------------------------------------- the title screen's medals
void drawEmblemRow(Canvas& c, int cy, const std::vector<std::pair<std::string, uint32_t>>& earned, uint32_t now) {
    if (earned.empty()) return;
    const int n = int(earned.size());
    const float r = n > 12 ? 8.f : 10.f, gap = r * 2.f + 5.f;
    float x = 320.f - gap * float(n - 1) / 2.f;
    for (const auto& e : earned) {
        emblems::draw(c, e.first, x, float(cy), r, e.second, emblems::kEarned, now);
        x += gap;
    }
}

void drawEmblemSheet(Canvas& c, uint32_t now) {
    heading(c, "League emblems", "earned (top), not yet earned and locked (bottom); drawn by the game at any size");
    const std::vector<const LeagueDef*>& all = league::all();
    int i = 0;
    for (const LeagueDef* d : all) {
        if (!d->emblem || !*d->emblem) continue;
        const int col = i % 4, row = i / 4;
        const float cx = 80.f + float(col) * 160.f, cy = 92.f + float(row) * 82.f;
        emblems::draw(c, d->emblem, cx, cy, 28.f, d->color, emblems::kEarned, now);
        drawTextAligned(c, FONT_XS, int(cx) - 78, int(cy) + 30, 156, Align::Center, emblems::name(d->emblem), theme::text);
        ++i;
    }
    const LeagueDef* g = league::find("gold");
    if (g) {
        emblems::draw(c, "gold", 560.f, 424.f, 14.f, g->color, emblems::kOpen, now);
        emblems::draw(c, "gold", 600.f, 424.f, 14.f, g->color, emblems::kLocked, now);
        drawTextAligned(c, FONT_XS, 380, 418, 150, Align::Right, "not yet earned / locked:", theme::faint);
    }
    drawEmblemRow(c, 424, {{"bronze", 0xC98A52}, {"silver", 0xAEB9C6}, {"lantern", 0x68FF92}, {"bolt", 0x2ED4EE}, {"gold", 0xE6B84A}}, now);
}

// ---------------------------------------------------------------- the league's rules
void drawRules(Canvas& c, const LeagueDef* d, uint32_t now) {
    if (!d) return;
    heading(c, "League rules", std::string(d->name) + " \xC2\xB7 first to " + std::to_string(d->target) + " points within " +
                                   std::to_string(d->maxGames) + " games");
    const Rect L{10, 50, 306, 386};
    drawPanel(c, L, theme::panel, theme::border, 10, 235);
    drawText(c, FONT_SMB, L.x + 10, L.y + 6, "How a league works", theme::gold);
    int y = L.y + 26;
    for (const std::string& l : wrapText(FONT_XS, league::pitch(), L.w - 20)) {
        drawText(c, FONT_XS, L.x + 10, y, l, rgb(200, 206, 222));
        y += 13;
    }
    y += 6;
    const std::string limit = "Reach " + std::to_string(d->target) + " points first to win the league. After game " + std::to_string(d->maxGames) +
                              " the most points wins; if that isn't you, the league starts again from 0 (your rivals remember).";
    for (const std::string& l : wrapText(FONT_XS, limit, L.w - 20)) {
        drawText(c, FONT_XS, L.x + 10, y, l, theme::text);
        y += 13;
    }
    y += 8;
    drawText(c, FONT_XS, L.x + 10, y, "Points for each place", theme::faint);
    y += 14;
    for (int p = 0; p < std::min(8, d->players); ++p) {
        const int x = L.x + 10 + (p % 4) * 72, yy = y + (p / 4) * 18;
        drawText(c, FONT_SMB, x, yy, ordinal(p), theme::dim);
        drawText(c, FONT_SMB, x + 30, yy, std::to_string(league::placePoints(p)), league::placePoints(p) ? theme::gold : theme::faint);
    }
    y += d->players > 4 ? 40 : 22;
    for (const std::string& l : wrapText(FONT_XS, league::tieRule(), L.w - 20)) {
        drawText(c, FONT_XS, L.x + 10, y, l, theme::dim);
        y += 13;
    }
    const Rect Rr{324, 50, 306, 386};
    drawPanel(c, Rr, theme::panel, mix(theme::border, d->color, 90), 10, 235);
    medal(c, d, float(Rr.x + 24), float(Rr.y + 24), 14.f, kOpen, now);
    drawText(c, FONT_SMB, Rr.x + 46, Rr.y + 8, "This league", theme::gold);
    drawText(c, FONT_XS, Rr.x + 46, Rr.y + 24, fitText(FONT_XS, modulesLine(d), Rr.w - 56), theme::faint);
    y = Rr.y + 46;
    const std::string lines[] = {
        std::to_string(d->players) + " players \xC2\xB7 " + std::to_string(d->vp) + " VP \xC2\xB7 " + league::difficultyName(d->difficulty) + " bots",
        std::string("Galaxy: ") + league::galaxyName(d->galaxy),
        d->factionDraft ? "A faction draft before every game" : "You keep one faction for the whole league",
        rulesLine(d),
        d->rivals > 0 ? std::to_string(d->rivals) + " rival commanders at each table" : "No named rivals",
    };
    for (const std::string& l : lines) {
        drawText(c, FONT_XS, Rr.x + 10, y, fitText(FONT_XS, l, Rr.w - 20), theme::text);
        y += 14;
    }
    y += 6;
    drawText(c, FONT_XS, Rr.x + 10, y, "Bonus points (+1 each)", theme::faint);
    y += 14;
    for (const league::BonusDef& b : league::bonuses()) {
        const bool te = (d->modules & ti::kModThundersEdge) != 0;
        const bool off = !te && (!std::strcmp(b.id, "mecstyx") || !std::strcmp(b.id, "fracture"));
        drawText(c, FONT_XS, Rr.x + 12, y, fitText(FONT_XS, std::string("\xC2\xB7 ") + b.label + (off ? " (Thunder's Edge)" : ""), Rr.w - 22),
                 off ? theme::faint : rgb(200, 206, 222));
        y += 13;
    }
    y += 8;
    drawText(c, FONT_XS, Rr.x + 10, y, "Reward", theme::gold);
    y += 14;
    for (const std::string& l : wrapText(FONT_XS, d->reward, Rr.w - 20)) {
        if (y > Rr.y + Rr.h - 14) break;
        drawText(c, FONT_XS, Rr.x + 10, y, l, theme::text);
        y += 13;
    }
    drawHintBar(c, {{"B", "Back"}});
}

void drawConfirm(Canvas& c, const std::string& title, const std::string& text, const std::string& yes) {
    c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 140);
    const Rect r{120, 150, 400, 160};
    drawPanel(c, r, theme::panel, theme::gold, 12, 250);
    drawTextAligned(c, FONT_LG, r.x, r.y + 14, r.w, Align::Center, title, theme::gold);
    int y = r.y + 50;
    for (const std::string& l : wrapText(FONT_SM, text, r.w - 40)) {
        drawTextAligned(c, FONT_SM, r.x, y, r.w, Align::Center, l, theme::text);
        y += 16;
    }
    hits::addBlock(r);
    c.fillRect(0, 444, 640, 36, rgb(6, 8, 16));  // (the screen's own hint bar underneath)
    drawHintBar(c, {{"A", yes}, {"B", "Cancel"}});
}

}  // namespace leagueui
