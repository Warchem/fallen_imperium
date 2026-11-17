#include "ui/deal_ui.h"

#include <cctype>

#include "ui/codec.h"
#include "ui/speech.h"
#include "ui/hits.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include "ai/ai.h"
#include "engine/deal.h"
#include "engine/personality.h"
#include "engine/query.h"
#include "galaxy/hex.h"
#include "ui/cards.h"
#include "gfx/font.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ti;

namespace {

constexpr float kPi = 3.14159265f;
const Rect kFull{4, 24, 632, 424};  // the deal screens use the whole screen above the hint bar

uint32_t pcol(const Game& g, int p) {
    return p == kNeutral ? theme::neutral : p >= 0 && p < g.numPlayers ? theme::player[g.players[p].color % 8] : theme::text;
}

uint32_t relationColor(Relation r) {
    switch (r) {
        case Relation::Promise: return rgb(70, 200, 240);
        case Relation::Contract: return rgb(80, 225, 120);
        case Relation::Hostile: return rgb(240, 70, 70);
        default: return rgb(120, 128, 150);
    }
}

std::string relationName(Relation r) {
    switch (r) {
        case Relation::Promise: return "A promise in force";
        case Relation::Contract: return "A deal in force";
        case Relation::Hostile: return "Clashed";
        default: return "No agreements";
    }
}

std::string sysLabel(const Game& g, int s) {
    if (s < 0 || s >= int(g.systems.size())) return "-";
    const SystemState& sy = g.systems[s];
    if (sy.home >= 0) return playerName(g, sy.home) + " home";
    const SystemDef& d = sy.d();
    if (d.planetCount == 0) return std::string(d.name) + " (" + d.key + ")";
    return d.name;
}

// Lines of text wrapped to a width, at most maxLines (the last one ends in "...").
std::vector<std::string> wrapMax(const FontData& f, const std::string& s, int w, int maxLines) {
    auto lines = wrapText(f, s, w);
    if (int(lines.size()) > maxLines) {
        lines.resize(size_t(maxLines));
        lines.back() = fitText(f, lines.back() + " ...", w);
    }
    return lines;
}

void fullPanel(Canvas& c) {
    c.fillRect(0, 22, 640, 430, rgb(4, 6, 12), 200);
    drawPanel(c, kFull, rgb(12, 17, 32), theme::gold, 10);
}

void errorBox(Canvas& c, const std::string& err, int x, int y, int w, bool warning) {
    if (err.empty()) return;
    auto lines = wrapMax(FONT_SM, err, w - 16, 2);
    int h = int(lines.size()) * 15 + 6;
    c.fillRoundRect(x, y - h, w, h, 6, warning ? rgb(60, 44, 12) : rgb(70, 18, 18), 235);
    int yy = y - h + 3;
    for (const std::string& l : lines) {
        drawText(c, FONT_SM, x + 8, yy, l, warning ? rgb(255, 225, 150) : rgb(255, 200, 200));
        yy += 15;
    }
}

// A faction disc: emblem in a ring of the player's colour.
void factionDisc(Canvas& c, const Game& g, int p, float x, float y, float r, int alpha = 255) {
    c.fillCircle(x, y, r, rgb(10, 14, 26), alpha);
    c.ring(x, y, r, 3.f, pcol(g, p), alpha);
    int s = int(r * 1.45f);
    drawFactionIcon(c, g.players[p].faction, int(x) - s / 2, int(y) - s / 2, s, alpha);
}

const char* fragmentIcon(int card) {
    static const char* k[4] = {"cultural", "hazardous", "industrial", "frontier"};
    return k[int(kExploreCards[card].type) & 3];
}

// One line of a side of a deal, with its icon.
struct Item {
    const char* icon = nullptr;
    std::string text, sub;
};

std::vector<Item> sideItems(const Game& g, const DealSide& s, int giver, int receiver) {
    std::vector<Item> v;
    if (s.tg) v.push_back({"trade_good", std::to_string(s.tg) + (s.tg == 1 ? " trade good" : " trade goods"), ""});
    if (s.comm)
        v.push_back({"commodity", std::to_string(s.comm) + (s.comm == 1 ? " commodity" : " commodities"),
                     "Become trade goods for " + playerName(g, receiver) + "."});
    if (s.note >= 0) v.push_back({"promissory", noteNameOf(g, s.note), noteTextOf(g, s.note)});
    std::map<int, int> frags;
    for (int f : s.fragments) ++frags[f];
    for (auto& f : frags)
        v.push_back({fragmentIcon(f.first), (f.second > 1 ? std::to_string(f.second) + "x " : std::string()) + kExploreCards[f.first].name, ""});
    for (int c : s.actionCards) v.push_back({"action_card", kActionCards[c].name, actionCardText(c)});
    for (int r : s.relics)
        if (r >= 0 && r < kRelicCount) v.push_back({"relic", kRelics[r].name, relicText(r)});
    for (int t : s.techs)
        if (t >= 0 && t < kTechCount) v.push_back({"cybernetic", std::string(kTechs[t].name) + " (shared)", techSummary(t)});
    for (int o : s.secrets)  // (only the receiver ever sees which one: the others see a count)
        v.push_back({"secret", (void(o), std::string("A secret objective")), "Unscored; it joins the receiver's secret objectives."});
    if (s.planet >= 0 && s.planet < int(g.planets.size()))
        v.push_back({"legendary", std::string("The planet ") + kPlanets[g.planets[s.planet].def].name,
                     playerName(g, giver) + "'s units there move to another of its planets."});
    if (!s.captured.empty())
        v.push_back({nullptr, std::to_string(s.captured.size()) + " captured unit" + (s.captured.size() > 1 ? "s" : "") + " returned", ""});
    for (int o : s.orders)  // Arms Dealers (Discordant Stars)
        v.push_back({"faction/axis", axisOrderName(o), axisOrderText(o)});
    if (s.hire)  // Hired Guns (Discordant Stars, 0.43.0)
        v.push_back({"faction/nokar", "Up to " + std::to_string(s.hire) + (s.hire == 1 ? " hired ship" : " hired ships"),
                     "Hired Guns: " + playerName(g, giver) + " picks the ships; they fly to the active system and fight as " +
                         playerName(g, receiver) + "'s. At the end of the action they may keep them (from their reinforcements)."});
    if (s.debt)  // Binding Debts (Discordant Stars)
        v.push_back({"faction/vaden", std::to_string(s.debt) + (s.debt == 1 ? " control token" : " control tokens") + " on the sheet",
                     playerName(g, giver) + "'s tokens go on " + playerName(g, receiver) + "'s faction sheet (Binding Debts)."});
    for (int a : s.laws)  // Encrypted Trade Hub (Discordant Stars, fixes 28): an agenda the giver holds
        if (a >= 0 && a < kAgendaCount) v.push_back({"agenda", std::string("The agenda ") + kAgendas[a].name, agendaText(a, false)});
    (void)giver;
    return v;
}

// =================================================================== web
}  // namespace

FactionWeb factionWebLayout(const Game& g, Rect area, int viewer) {
    FactionWeb w;
    w.n = g.numPlayers;
    w.cx = area.x + area.w / 2.f;
    w.cy = area.y + area.h / 2.f;
    float rx = area.w / 2.f - 44.f, ry = area.h / 2.f - 42.f;
    float mx = 0, my = 0;
    int mec = g.mecatol();
    if (mec >= 0) hexToPixel(g.systems[mec].pos, 1.f, mx, my);
    float ang[8] = {};
    for (int p = 0; p < w.n; ++p) {
        int h = g.players[p].homeSystem;
        float hx = 0, hy = 0;
        if (h >= 0) hexToPixel(g.systems[h].pos, 1.f, hx, hy);
        if (h >= 0 && (std::fabs(hx - mx) > 0.01f || std::fabs(hy - my) > 0.01f)) ang[p] = std::atan2(hy - my, hx - mx);
        else ang[p] = 2.f * kPi * float(p) / float(std::max(1, w.n)) - kPi / 2.f;
    }
    float rot = (viewer >= 0 && viewer < w.n) ? kPi / 2.f - ang[viewer] : 0.f;
    for (int p = 0; p < w.n; ++p) {
        float a = ang[p] + rot;
        w.x[p] = w.cx + rx * std::cos(a);
        w.y[p] = w.cy + ry * std::sin(a);
    }
    return w;
}

void drawSpeakerToken(Canvas& c, float x, float top, float w, int alpha, uint32_t now);

void drawFactionWeb(Canvas& c, const Game& g, const FactionWeb& w, int viewer, int selected, uint32_t now,
                    const std::vector<bool>* enabled) {
    const float pulse = 0.6f + 0.4f * std::sin(float(now % 1600) / 1600.f * 2.f * kPi);
    // Mecatol Rex in the middle
    int mec = g.mecatol();
    int mo = -1;
    if (mec >= 0)
        for (int16_t x : g.systems[mec].planets)
            if (g.planets[x].owner >= 0) mo = g.planets[x].owner;
    c.fillCircle(w.cx, w.cy, 13.f, rgb(26, 32, 54));
    c.ring(w.cx, w.cy, 13.f, 2.f, mo >= 0 ? pcol(g, mo) : rgb(150, 140, 110));
    drawTextAligned(c, FONT_XS, int(w.cx) - 20, int(w.cy) - 7, 40, Align::Center, "MR", theme::dim);
    // lines: plain ones first, then coloured relations on top
    for (int pass = 0; pass < 2; ++pass)
        for (int a = 0; a < w.n; ++a)
            for (int b = a + 1; b < w.n; ++b) {
                if (g.players[a].eliminated || g.players[b].eliminated) continue;
                Relation r = relationOf(g, a, b);
                if ((r == Relation::None) != (pass == 0)) continue;
                bool focus = selected < 0 || a == selected || b == selected;
                uint32_t col = relationColor(r);
                if (r == Relation::None) {
                    bool nb = areNeighbours(g, a, b);
                    int al = nb ? 175 : 55;
                    if (!focus) al = al * 2 / 3;
                    c.line(w.x[a], w.y[a], w.x[b], w.y[b], col, nb ? 2.f : 1.f, al);
                    continue;
                }
                int al = focus ? int(255 * (selected >= 0 ? pulse : 1.f)) : 90;
                c.line(w.x[a], w.y[a], w.x[b], w.y[b], col, 7.f, al / 6);  // glow
                c.line(w.x[a], w.y[a], w.x[b], w.y[b], col, 2.5f, al);
            }
    for (int p = 0; p < w.n; ++p) {
        bool on = !enabled || (p < int(enabled->size()) && (*enabled)[p]) || p == viewer;
        int alpha = g.players[p].eliminated ? 70 : on ? 255 : 120;
        if (p == selected) {
            c.fillCircle(w.x[p], w.y[p], 31.f, theme::gold, int(60 * pulse));
            c.ring(w.x[p], w.y[p], 28.f, 2.5f, theme::gold, int(255 * pulse));
        }
        factionDisc(c, g, p, w.x[p], w.y[p], 22.f, alpha);
        // the strategy cards they hold this round (owner, 0.19.0), above the circle; played ones dimmed
        {
            const auto& held = g.players[p].cards;
            const int cw = 46, ch = cards::strategyHeight(cw), gap = 3;
            const int total = int(held.size()) * cw + std::max(0, int(held.size()) - 1) * gap;
            int cx0 = int(w.x[p]) - total / 2;
            for (int8_t card : held) {
                const bool used = (g.players[p].cardsUsed >> card) & 1;
                cards::strategy(c, card, cx0, int(w.y[p]) - 22 - ch - 3, cw, used ? alpha * 2 / 5 : alpha);
                cx0 += cw + gap;
            }
        }
        std::string name = p == viewer ? std::string("You") : playerName(g, p);
        if (g.currentAgenda >= 0) {  // before a vote: what each player can put in
            int v = availableVotes(g, p);
            name += " \xC2\xB7 " + std::to_string(v) + (v == 1 ? " vote" : " votes");
        } else {
            name += " \xC2\xB7 " + std::to_string(g.players[p].vp) + " VP";
        }
        int tw = textWidth(FONT_XS, name);
        int ty = int(w.y[p]) + 25;
        std::string arch;  // after round 3: the archetype, and how it feels about the viewer
        if (archetypesRevealed(g) && isBot(g, p) && p != viewer) {
            arch = archName(g, p);
            if (viewer >= 0 && !isBot(g, viewer)) arch += std::string(" \xC2\xB7 ") + attitudeName(attitudeOf(g, p, viewer));
        }
        const int aw = arch.empty() ? 0 : textWidth(FONT_XS, arch);
        const int bw = std::max(tw, aw);
        c.fillRoundRect(int(w.x[p]) - bw / 2 - 4, ty, bw + 8, arch.empty() ? 14 : 27, 4, rgb(4, 6, 12), 170 * alpha / 255);
        drawText(c, FONT_XS, int(w.x[p]) - tw / 2, ty, name, p == viewer ? theme::text : pcol(g, p), alpha);
        if (!arch.empty()) {
            Attitude at = viewer >= 0 ? attitudeOf(g, p, viewer) : Attitude::Wary;
            uint32_t ac = at == Attitude::Friendly ? theme::green : at == Attitude::Hostile ? theme::red : rgb(230, 200, 110);
            drawText(c, FONT_XS, int(w.x[p]) - aw / 2, ty + 13, arch, ac, alpha);
        }
        if (p == g.speaker) drawSpeakerToken(c, w.x[p], float(ty + (arch.empty() ? 16 : 29)), 64.f, alpha, now);
    }
}

// The speaker token (0.28.0, owner's picture): a dark plate with bevelled ends, red lights
// along its top and bottom edges and SPEAKER across it. (x, top) = its top centre.
void drawSpeakerToken(Canvas& c, float x, float top, float w, int alpha, uint32_t now) {
    const float h = w * 0.36f, b = h * 0.32f;
    const float x0 = x - w / 2, x1 = x + w / 2, y0 = top, y1 = top + h;
    const float xs[8] = {x0 + b, x1 - b, x1, x1, x1 - b, x0 + b, x0, x0};
    const float ys[8] = {y0, y0, y0 + b, y1 - b, y1, y1, y1 - b, y0 + b};
    c.fillPolygon(xs, ys, 8, rgb(150, 20, 24), alpha);  // the red rim
    const float in = std::max(1.5f, w * 0.03f);
    const float xi[8] = {x0 + b + in * 0.4f, x1 - b - in * 0.4f, x1 - in, x1 - in, x1 - b - in * 0.4f, x0 + b + in * 0.4f, x0 + in, x0 + in};
    const float yi[8] = {y0 + in, y0 + in, y0 + b, y1 - b, y1 - in, y1 - in, y1 - b, y0 + b};
    c.fillPolygon(xi, yi, 8, rgb(16, 18, 26), alpha);
    // a few stars on the plate
    for (int i = 0; i < 7; ++i) c.fillRect(int(x0 + w * (0.18f + 0.11f * float(i))), int(y0 + h * (0.25f + 0.5f * float((i * 5) % 3) / 2.f)), 1, 1, rgb(160, 170, 200), alpha / 2);
    // the lights: three along the top, three along the bottom, softly pulsing
    const float pulse = 0.75f + 0.25f * std::sin(float(now % 1600) / 1600.f * 6.2831853f);
    for (int i = -1; i <= 1; ++i) {
        const float lx = x + float(i) * w * 0.25f;
        for (float ly : {y0 + in * 0.6f, y1 - in * 0.6f}) {
            c.fillCircle(lx, ly, h * 0.2f, rgb(255, 40, 40), int(float(alpha) * 0.35f * pulse));
            c.fillCircle(lx, ly, h * 0.11f, rgb(255, 90, 80), alpha);
        }
    }
    drawTextAligned(c, w >= 90.f ? FONT_SMB : FONT_XS, int(x0), int(y0 + h / 2 - (w >= 90.f ? 7 : 5)), int(w), Align::Center, "SPEAKER",
                    rgb(240, 242, 250), alpha);
}

void drawWebLegend(Canvas& c, int x, int y) {
    struct L {
        Relation r;
        const char* t;
    } items[] = {{Relation::None, "neighbours"}, {Relation::Promise, "promise"}, {Relation::Contract, "deal"},
                 {Relation::Hostile, "clashed"}};
    for (const L& l : items) {
        uint32_t col = relationColor(l.r);
        c.line(float(x), float(y + 7), float(x + 16), float(y + 7), col, l.r == Relation::None ? 1.6f : 2.5f,
               l.r == Relation::None ? 160 : 255);
        x += 20;
        drawText(c, FONT_XS, x, y, l.t, theme::dim);
        x += textWidth(FONT_XS, l.t) + 12;
    }
}

int webStep(const FactionWeb& w, int from, int dx, int dy, const std::vector<bool>& allowed) {
    if (from < 0 || from >= w.n) {
        for (int p = 0; p < w.n; ++p)
            if (p < int(allowed.size()) && allowed[p]) return p;
        return from;
    }
    int best = -1;
    float bestScore = 1e9f;
    for (int p = 0; p < w.n; ++p) {
        if (p == from || p >= int(allowed.size()) || !allowed[p]) continue;
        float vx = w.x[p] - w.x[from], vy = w.y[p] - w.y[from];
        float d = std::sqrt(vx * vx + vy * vy);
        if (d < 1.f) continue;
        float cosA = (vx * dx + vy * dy) / d;
        if (cosA < 0.25f) continue;
        float score = d / (cosA + 0.15f);
        if (score < bestScore) {
            bestScore = score;
            best = p;
        }
    }
    if (best >= 0) return best;
    // nothing that way: step around the circle instead
    int n = w.n;
    int dir = (dx > 0 || dy > 0) ? 1 : -1;
    // order the players by angle around the centre
    std::vector<int> order;
    for (int p = 0; p < n; ++p) order.push_back(p);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::atan2(w.y[a] - w.cy, w.x[a] - w.cx) < std::atan2(w.y[b] - w.cy, w.x[b] - w.cx);
    });
    int i = int(std::find(order.begin(), order.end(), from) - order.begin());
    for (int k = 1; k < n; ++k) {
        int p = order[((i + dir * k) % n + n) % n];
        if (p < int(allowed.size()) && allowed[p]) return p;
    }
    return from;
}

void drawRelationsPanel(Canvas& c, const Game& g, Rect r, int me, int q) {
    drawPanel(c, r, theme::panel, theme::border, 8);
    if (q < 0 || q >= g.numPlayers) {
        drawTextAligned(c, FONT_SM, r.x, r.y + 20, r.w, Align::Center, "Choose a faction", theme::dim);
        return;
    }
    const Player& pl = g.players[q];
    factionDisc(c, g, q, float(r.x + 30), float(r.y + 30), 21.f);
    const int tx = r.x + 58, tw = r.w - 64;
    drawText(c, FONT_MDB, tx, r.y + 9, fitText(FONT_MDB, q == me ? std::string("You") : playerName(g, q), tw), pcol(g, q));
    drawText(c, FONT_XS, tx, r.y + 30, fitText(FONT_XS, kFactions[pl.faction].name, tw), theme::faint);
    int y = r.y + 58;
    const int x = r.x + 10, w = r.w - 20;
    // public numbers
    int ix = x;
    auto stat = [&](const char* icon, const std::string& v) {
        if (icons::draw(c, icon, ix, y, 15)) ix += 17;
        drawText(c, FONT_SMB, ix, y, v, theme::text);
        ix += textWidth(FONT_SMB, v) + 10;
    };
    drawText(c, FONT_SMB, ix, y, std::to_string(pl.vp) + " VP", theme::gold);
    ix += textWidth(FONT_SMB, std::to_string(pl.vp) + " VP") + 10;
    stat("trade_good", std::to_string(pl.tradeGoods));
    stat("commodity", std::to_string(pl.commodities) + "/" + std::to_string(commodityValue(g, pl)));
    stat("promissory", std::to_string(pl.notes.size()));
    y += 22;
    if (me < 0 || me >= g.numPlayers) return;  // an all-computer test game: nobody to relate to
    if (g.currentAgenda >= 0) {
        drawText(c, FONT_XS, x, y, "Votes on this agenda: " + std::to_string(availableVotes(g, q)) +
                                       (q != me ? " (you: " + std::to_string(availableVotes(g, me)) + ")" : ""), theme::cyan);
        y += 15;
    }
    if (q == me) {
        int broken = pl.promisesBroken;
        drawText(c, FONT_XS, x, y, "Promises you have broken: " + std::to_string(broken), theme::dim);
        y += 16;
        int active = 0;
        for (const Promise& p : g.promises)
            if (p.state == kPromiseActive && (p.by == me || p.to == me)) ++active;
        drawText(c, FONT_XS, x, y, "Promises in force: " + std::to_string(active), theme::dim);
        return;
    }
    // personality (revealed after round 3)
    if (isBot(g, q)) {
        if (archetypesRevealed(g)) {
            const ArchDef& ad = kArchetypes[pl.pers.arch];
            drawText(c, FONT_SMB, x, y, ad.name, rgb(255, 214, 120));
            if (!isBot(g, me)) {
                Attitude at = attitudeOf(g, q, me);
                uint32_t ac = at == Attitude::Friendly ? theme::green : at == Attitude::Hostile ? theme::red : rgb(230, 200, 110);
                drawTextAligned(c, FONT_SMB, x, y, w, Align::Right, attitudeName(at), ac);
            }
            y += 15;
            for (const std::string& l : wrapMax(FONT_XS, ad.blurb, w, 2)) {
                drawText(c, FONT_XS, x, y, l, theme::dim);
                y += 12;
            }
            y += 3;
        } else {
            drawText(c, FONT_XS, x, y, "Personality hidden until the end of round 3", theme::faint);
            y += 15;
        }
    }
    // trading
    std::string trade;
    uint32_t tcol = theme::dim;
    if (me < 0 || me >= g.numPlayers) {
        trade = "";
    } else if (transactionAllowed(g, me, q)) {
        trade = "You can trade components now.";
        tcol = theme::green;
    } else if (g.transactions[me][q] + g.transactions[q][me] > 0) {
        trade = "You have already traded this turn.";
    } else if (areNeighbours(g, me, q)) {
        trade = "Neighbours: trade on either player's turn, or before a vote.";
    } else {
        trade = "Not neighbours: promises only (anyone can trade before a vote).";
    }
    for (const std::string& l : wrapMax(FONT_XS, trade, w, 2)) {
        drawText(c, FONT_XS, x, y, l, tcol);
        y += 13;
    }
    if (me >= 0 && me < g.numPlayers && ((g.phase == Phase::Action && g.activePlayer == me) || (g.currentAgenda >= 0))) {
        int left = offersLeft(g, me, q);
        drawText(c, FONT_XS, x, y, std::to_string(left) + (left == 1 ? " offer" : " offers") + " left this " +
                                       (g.phase == Phase::Agenda ? "agenda" : "turn"), theme::faint);
        y += 13;
    }
    y += 4;
    // relation
    Relation rel = relationOf(g, me, q);
    c.line(float(x), float(y + 7), float(x + 18), float(y + 7), relationColor(rel), 2.5f);
    drawText(c, FONT_SMB, x + 24, y, relationName(rel), relationColor(rel));
    y += 17;
    if (pl.promisesBroken > 0) {
        drawText(c, FONT_XS, x, y, "Has broken " + std::to_string(pl.promisesBroken) + (pl.promisesBroken == 1 ? " promise" : " promises"),
                 rgb(240, 150, 150));
        y += 14;
    }
    const int bottom = r.y + r.h - 8;
    // promises between us
    auto prom = activePromises(g, me, q);
    if (!prom.empty() && y + 30 < bottom) {
        y += 4;
        drawText(c, FONT_SMB, x, y, "Promises", theme::cyan);
        y += 16;
        for (const Promise* p : prom) {
            for (const std::string& l : wrapMax(FONT_XS, promiseText(g, *p), w, 3)) {
                if (y + 13 > bottom) break;
                drawText(c, FONT_XS, x, y, l, p->by == me ? theme::text : pcol(g, q));
                y += 13;
            }
            y += 3;
        }
    }
    // notes each holds from the other (face up)
    std::vector<std::string> inPlay;
    if (me >= 0 && me < g.numPlayers)  // no seat when watching an all-computer test game
        for (int16_t n : g.players[me].notesInPlay)
            if (noteOwner(n) == q) inPlay.push_back("You have their " + noteNameOf(g, n) + " in play");
    for (int16_t n : pl.notesInPlay)
        if (noteOwner(n) == me) inPlay.push_back("They have your " + noteNameOf(g, n) + " in play");
    for (const std::string& s : inPlay)
        for (const std::string& l : wrapMax(FONT_XS, s, w, 2)) {
            if (y + 13 > bottom) break;
            drawText(c, FONT_XS, x, y, l, theme::green);
            y += 13;
        }
    // past deals
    std::vector<const DealRecord*> recs;
    for (const DealRecord& d : g.deals)
        if ((d.from == me && d.to == q) || (d.from == q && d.to == me)) recs.push_back(&d);
    if (!recs.empty() && y + 30 < bottom) {
        y += 4;
        drawText(c, FONT_SMB, x, y, "Deals", theme::cyan);
        y += 16;
        for (int i = int(recs.size()) - 1; i >= 0 && y + 13 <= bottom; --i) {
            const DealRecord& d = *recs[i];
            static const char* kRes[4] = {"", "deal made", "declined", "fell through"};
            std::string head = "Round " + std::to_string(d.round) + " \xC2\xB7 " + kRes[std::min<int>(3, d.result)] +
                               (d.from == me ? " (your offer)" : " (their offer)");
            drawText(c, FONT_XS, x, y, fitText(FONT_XS, head, w), d.result == 1 ? theme::green : theme::dim);
            y += 13;
            for (const std::string& l : wrapMax(FONT_XS, d.text, w - 8, 2)) {
                if (y + 13 > bottom) break;
                drawText(c, FONT_XS, x + 8, y, l, theme::faint);
                y += 13;
            }
        }
    }
}

namespace {

// =================================================================== negotiate: pick a player on the web
class NegotiateUI : public DecisionUI {
public:
    bool centred() const override { return true; }  // (widescreen: the whole 640 screen, centred)
    NegotiateUI(const Game& g, const Decision& d) : DecisionUI(g, d) {
        web_ = factionWebLayout(g, Rect{4, 70, 410, 362}, d.player);
        allowed_.assign(size_t(g.numPlayers), false);
        for (const Option& o : d_.options)
            if (o.id >= 0 && o.id < g.numPlayers && o.enabled) allowed_[size_t(o.id)] = true;
        sel_ = -1;
        // start on a player you can trade with, nearest the bottom-centre
        float best = 1e9f;
        for (int p = 0; p < g.numPlayers; ++p)
            if (allowed_[size_t(p)]) {
                float dd = std::fabs(web_.x[p] - web_.cx) + std::fabs(web_.y[p] - (web_.cy + 200)) +
                           (transactionAllowed(g, d.player, p) ? 0.f : 2000.f);
                if (dd < best) {
                    best = dd;
                    sel_ = p;
                }
            }
        for (const Option& o : d_.options)
            if (o.id == -1) doneLabel_ = o.label;
    }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        return {{"D-PAD", "Choose"}, {"A", "Make an offer"}, {"B", doneLabel_.empty() ? "Done" : "Done"}};
    }

    bool update(const Input& in, uint32_t) override {
        int dx = 0, dy = 0;
        if (in.pressed(Btn::Left)) dx = -1;
        if (in.pressed(Btn::Right)) dx = 1;
        if (in.pressed(Btn::Up)) dy = -1;
        if (in.pressed(Btn::Down)) dy = 1;
        if (dx || dy) sel_ = webStep(web_, sel_, dx, dy, allowed_);
        if (in.pressed(Btn::A)) {
            if (sel_ >= 0 && allowed_[size_t(sel_)]) {
                answer_ = Answer();
                answer_.picks = {sel_};
                return true;
            }
            setError("There's no one you can make an offer to right now.");
        }
        if (in.pressed(Btn::B)) {
            answer_ = Answer();
            answer_.picks = {-1};
            return true;
        }
        return false;
    }

    void draw(Canvas& c, uint32_t now) override {
        fullPanel(c);
        drawText(c, FONT_LG, kFull.x + 12, kFull.y + 6, fitText(FONT_LG, d_.prompt, 390), theme::gold);
        if (!d_.context.empty())
            drawText(c, FONT_XS, kFull.x + 12, kFull.y + 32, fitText(FONT_XS, d_.context, 396), theme::dim);
        drawFactionWeb(c, g_, web_, d_.player, sel_, now, &allowed_);
        for (int q = 0; q < web_.n; ++q)  // the mouse (0.28.0): point at a faction, click to choose it
            if (q != d_.player && q < int(allowed_.size()) && allowed_[size_t(q)])
                hits::add(Rect{int(web_.x[q]) - 26, int(web_.y[q]) - 26, 52, 52}, [this, q]() { sel_ = q; });
        drawWebLegend(c, kFull.x + 12, kFull.y + kFull.h - 20);
        drawRelationsPanel(c, g_, Rect{420, 30, 210, 412}, d_.player, sel_);
        errorBox(c, error_, kFull.x + 8, kFull.y + kFull.h - 26, 400, false);
    }

private:
    FactionWeb web_;
    std::vector<bool> allowed_;
    int sel_ = -1;
    std::string doneLabel_;
};

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper((unsigned char)ch));
    return s;
}

// =================================================================== the answer to an offer
// 0.57.0: the comms codec (owner: Metal Gear-style, space themed) - the offering faction's portrait and the
// player's, the subspace signal, the offer's words typed out in that faction's voice, the deal underneath.
class DealResponseUI : public DecisionUI {
public:
    bool centred() const override { return true; }  // (widescreen: the whole 640 screen, centred)
    DealResponseUI(const Game& g, const Decision& d, MapView& map) : DecisionUI(g, d), map_(map) {
        decodeDeal(d_.deal, deal_);
        for (size_t i = 0; i < d_.options.size(); ++i)
            if (d_.options[i].enabled) {
                cursor_ = int(i);
                break;
            }
        std::vector<int> sys;
        for (const Promise& p : deal_.promises)
            if (promiseHasSystem(p.kind) && p.target >= 0) sys.push_back(p.target);
        if (deal_.threat >= 0) sys.push_back(deal_.threat);
        if (!sys.empty()) {
            map_.setHighlights(sys);
            map_.setCursor(g, sys[0]);
            highlighted_ = true;
        }
        // what the offering faction says (display only: chosen by a hash, never the game's dice)
        const int me = d_.player, them = deal_.from;
        bool counter = true;
        for (const Option& o : d_.options)
            if (o.id == 2) counter = false;  // (an original offer can be countered; a counter-offer can't)
        bool threat = deal_.threat >= 0;
        for (const Promise& p : deal_.promises)
            if (p.kind == PromiseKind::LeaveSystem && p.by == me) threat = true;
        cat_ = counter ? speech::Cat::Counter : threat ? speech::Cat::Threat : d_.agenda >= 0 ? speech::Cat::Vote : speech::Cat::Offer;
        if (cat_ == speech::Cat::Offer && me >= 0 && them >= 0) {
            const Relation rel = relationOf(g, me, them);
            if (rel == Relation::Contract || rel == Relation::Promise) cat_ = speech::Cat::Friendly;
        }
        {
            uint32_t h = uint32_t(g.round) * 977U + g.seed;
            for (int v : d_.deal) h = speech::seedOf(h, uint32_t(v));
            seed_ = speech::seedOf(h, uint32_t(d_.player), uint32_t(deal_.from));
        }
        speaks_ = them >= 0 && them < g.numPlayers && me >= 0 && me < g.numPlayers && !g.players[them].human;
        if (speaks_) line_ = speech::pick(g.players[them].faction, g.players[me].faction, cat_, seed_);
        if (line_.empty()) speaks_ = false;
    }
    ~DealResponseUI() override {
        if (highlighted_) map_.clearHighlights();
    }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        return {{"\xE2\x97\x80\xE2\x96\xB6", "Choose"}, {"A", "Confirm"}, {"B", "Decline"}};
    }

    bool update(const Input& in, uint32_t) override {
        const int n = int(d_.options.size());
        if (n == 0) return false;
        if (in.pressed(Btn::Left) || in.pressed(Btn::Up)) cursor_ = (cursor_ + n - 1) % n;
        if (in.pressed(Btn::Right) || in.pressed(Btn::Down)) cursor_ = (cursor_ + 1) % n;
        if (in.pressed(Btn::A)) {
            const Option& o = d_.options[size_t(cursor_)];
            if (!o.enabled) {
                setError(o.detail.empty() ? "You can't accept this." : o.detail);
                return false;
            }
            answer_ = Answer();
            answer_.picks = {o.id};
            return true;
        }
        if (in.pressed(Btn::B)) {
            answer_ = Answer();
            answer_.picks = {0};
            return true;
        }
        return false;
    }

    void draw(Canvas& c, uint32_t now) override {
        if (start_ == 0) start_ = now | 1U;
        const uint32_t age = now - start_;
        const int me = d_.player, them = deal_.from;
        const bool alert = cat_ == speech::Cat::Threat;
        codec::backdrop(c, now, alert, 22, 452);
        const uint32_t themCol = them >= 0 && them < g_.numPlayers ? pcol(g_, them) : theme::gold;
        const uint32_t meCol = me >= 0 && me < g_.numPlayers ? pcol(g_, me) : theme::cyan;
        // title
        drawText(c, FONT_LG, 16, 25, cat_ == speech::Cat::Counter ? "COUNTER-OFFER" : alert ? "A DEMAND" : "NEGOTIATIONS",
                 alert ? rgb(255, 90, 80) : rgb(255, 90, 200));
        drawText(c, FONT_XS, 16, 49, fitText(FONT_XS, d_.prompt + " over the subspace channel", 300), rgb(170, 175, 215));
        // the two windows and their plates
        const Rect lw{14, 64, 132, 132}, rw{494, 64, 132, 132};
        if (them >= 0 && them < g_.numPlayers) {
            codec::window(c, g_.players[them].faction, lw, themCol, now, age, 255, 0, alert);
            const Relation rel = relationOf(g_, me, them);
            std::string sub = std::string(speaks_ && shown(now) < speech_.chars ? "Speaking" : "Waiting");
            if (rel != Relation::None) sub += std::string(" \xC2\xB7 ") + relationName(rel);
            codec::plate(c, g_.players[them].faction, Rect{14, 200, 132, 30}, upper(playerName(g_, them)), sub, themCol);
        }
        if (me >= 0 && me < g_.numPlayers) {
            codec::window(c, g_.players[me].faction, rw, meCol, now, age > 120 ? age - 120 : 0, 255, 1500);
            codec::plate(c, g_.players[me].faction, Rect{494, 200, 132, 30}, upper(playerName(g_, me)), "You", meCol);
        }
        // the signal and the speech
        const Rect box{152, 122, 336, 108};
        if (speech_.words.empty() && speaks_) {
            const Rect a = codec::speechArea(box);
            speech_ = speech::layoutFit(g_.players[them].faction, line_, a.w, a.h, themCol, rgb(236, 238, 250), seed_);
        }
        const int sh = shown(now);
        codec::signal(c, 320, 62, codec::frequency(seed_), alert ? rgb(255, 120, 110) : rgb(110, 240, 210), now,
                      speaks_ && sh >= 0 && sh < speech_.chars, false);
        if (speaks_) {
            codec::speechBox(c, box, upper(playerName(g_, them)), themCol, speech_, sh, now, alert);
        } else {
            c.fillRoundRect(box.x, box.y, box.w, box.h, 8, rgb(8, 6, 22), 235);
            codec::glowFrame(c, box, themCol, 8);
            drawText(c, FONT_SMB, box.x + 10, box.y + 5, playerName(g_, them), themCol);
            for (const std::string& l : wrapMax(FONT_SM, d_.prompt + ".", box.w - 20, 3))
                drawText(c, FONT_SM, box.x + 10, box.y + 26, l, rgb(220, 224, 240));
        }
        // the deal
        const Rect r{8, 236, 624, 212};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 10, rgb(8, 10, 26), 228);
        codec::glowFrame(c, r, rgb(110, 120, 220), 10, 160);
        int y = r.y + 8;
        if (them >= 0 && g_.players[them].promisesBroken > 0) {
            drawTextAligned(c, FONT_XS, r.x, r.y + 8, r.w - 12, Align::Right,
                            playerName(g_, them) + " has broken " + std::to_string(g_.players[them].promisesBroken) +
                                (g_.players[them].promisesBroken == 1 ? " promise." : " promises."),
                            rgb(240, 150, 150));
        }
        const int colW = (r.w - 30) / 2;
        const int bottom = r.y + r.h - 62;
        auto column = [&](int x, const std::string& title, const std::vector<Item>& items, uint32_t col) {
            int yy = y;
            drawText(c, FONT_SMB, x, yy, title, col);
            yy += 17;
            if (items.empty()) {
                drawText(c, FONT_SM, x + 4, yy, "nothing", theme::faint);
                yy += 15;
            }
            for (const Item& it : items) {
                if (yy + 15 > bottom) break;
                int lx = x + 4;
                if (it.icon && icons::draw(c, it.icon, lx, yy, 15)) lx += 19;
                drawText(c, FONT_SM, lx, yy, fitText(FONT_SM, it.text, x + colW - lx), theme::text);
                yy += 15;
                for (const std::string& l : wrapMax(FONT_XS, it.sub, colW - 24, 2)) {
                    if (yy + 12 > bottom) break;
                    drawText(c, FONT_XS, x + 24, yy, l, theme::dim);
                    yy += 12;
                }
                yy += 2;
            }
            return yy;
        };
        int y1 = column(r.x + 14, playerName(g_, them) + " gives you", sideItems(g_, deal_.give, them, me), themCol);
        int y2 = column(r.x + 20 + colW, "You give " + playerName(g_, them), sideItems(g_, deal_.get, me, them), theme::text);
        c.vline(r.x + 16 + colW, y, std::max(y1, y2), rgb(70, 80, 150));
        y = std::max(y1, y2) + 4;
        if (!deal_.promises.empty() && y + 15 <= bottom) {
            c.hline(r.x + 10, r.x + r.w - 10, y, rgb(70, 80, 150));
            y += 4;
            drawText(c, FONT_SMB, r.x + 14, y, "Promises (not binding - the game notes whether they are kept)", theme::cyan);
            y += 16;
            for (const Promise& p0 : deal_.promises) {
                Promise p = p0;
                p.to = int8_t(p.by == them ? me : them);
                std::string t = (p.by == me ? std::string("You promise: ") : playerName(g_, them) + " promises: ") + promiseText(g_, p, false);
                for (const std::string& l : wrapMax(FONT_SM, t, r.w - 30, 2)) {
                    if (y + 15 > bottom) break;
                    drawText(c, FONT_SM, r.x + 18, y, l, p.by == me ? theme::text : themCol);
                    y += 15;
                }
            }
        }
        if (deal_.threat >= 0 && y + 15 <= bottom) {
            y += 2;
            drawText(c, FONT_SMB, r.x + 14, y,
                     fitText(FONT_SMB, "If you refuse, " + playerName(g_, them) + " may attack " + sysLabel(g_, deal_.threat) + ".", r.w - 28),
                     rgb(240, 110, 110));
        }
        {
            const Player& mp = g_.players[me];
            std::string have = "You have " + std::to_string(mp.tradeGoods) + (mp.tradeGoods == 1 ? " trade good" : " trade goods") + " and " +
                               std::to_string(mp.commodities) + (mp.commodities == 1 ? " commodity." : " commodities.");
            drawText(c, FONT_XS, r.x + 14, r.y + r.h - 56, have, theme::dim);
            if (cursor_ >= 0 && cursor_ < int(d_.options.size()))
                drawTextAligned(c, FONT_XS, r.x, r.y + r.h - 56, r.w - 14, Align::Right,
                                fitText(FONT_XS, d_.options[size_t(cursor_)].detail, r.w / 2), theme::dim);
        }
        // the buttons
        const int n = int(d_.options.size());
        const int bw = 150, gap = 14, total = n * bw + (n - 1) * gap;
        int bx = r.x + (r.w - total) / 2, by = r.y + r.h - 38;
        for (int i = 0; i < n; ++i) {
            const Option& o = d_.options[size_t(i)];
            bool sel = i == cursor_;
            uint32_t base = o.id == 1 ? theme::green : o.id == 0 ? theme::red : theme::cyan;
            hits::add(Rect{bx, by, bw, 28}, [this, i]() { cursor_ = i; });  // the mouse (0.29.0): a click picks THIS button
            c.fillRoundRect(bx, by, bw, 28, 8, sel ? shade(base, 0.45f) : rgb(14, 18, 36), 240);
            if (sel) codec::glowFrame(c, Rect{bx, by, bw, 28}, base, 8);
            else c.frameRoundRect(bx, by, bw, 28, 8, shade(base, 0.7f), 1);
            drawTextAligned(c, FONT_MDB, bx, by + 5, bw, Align::Center, o.label, !o.enabled ? theme::faint : sel ? theme::text : theme::dim);
            bx += bw + gap;
        }
        errorBox(c, error_, r.x + 8, by - 4, r.w - 16, false);
    }

private:
    // characters typed so far: the windows switch on first, then the line types out
    int shown(uint32_t now) const {
        if (start_ == 0) return 0;
        const uint32_t age = now - start_;
        return age < 450 ? 0 : speech::typed(speech_, age - 450);
    }

    MapView& map_;
    Deal deal_;
    int cursor_ = 0;
    bool highlighted_ = false;
    speech::Cat cat_ = speech::Cat::Offer;
    uint32_t seed_ = 0, start_ = 0;
    bool speaks_ = false;
    std::string line_;
    speech::Speech speech_;
};

// =================================================================== the deal composer
class DealUI : public DecisionUI {
public:
    bool centred() const override { return true; }  // (widescreen: the whole 640 screen, centred)
    DealUI(const Game& g, const Decision& d, MapView& map) : DecisionUI(g, d), map_(map) {
        me_ = d.player;
        them_ = d.targetPlayer;
        counter_ = (d.dealFlags & kDealCounter) != 0;
        components_ = (d.dealFlags & kDealComponents) != 0;
        Deal base;
        if (counter_ && decodeDeal(d_.deal, base)) {
            base_ = base;
            Deal mirrored;
            mirrored.from = me_;
            mirrored.to = them_;
            mirrored.give = base.get;
            mirrored.get = base.give;
            mirrored.promises = base.promises;
            load(mirrored);
        } else if (!counter_ && !d_.deal.empty() && decodeDeal(d_.deal, base)) {
            load(base);  // an offer that starts filled in (Hired Guns, 0.43.0)
        }
        // before a vote, an offer starts as "vote for the outcome you want"
        const bool voteDefault = !counter_ && (d.dealFlags & kDealVote) && d.agenda >= 0;
        if (voteDefault) {
            slots_[0].kind = PromiseKind::VoteFor;
            slots_[0].mine = false;
            slots_[0].out = aiFavouredOutcome(g, me_, d.agenda);
            if (slots_[0].out < 0) slots_[0].kind = PromiseKind::None;
        }
        rebuild();
        cursor_ = 0;
        moveCursor(1, true);
        if (voteDefault) keepCursor("prom.0.out");
        changed();
    }
    ~DealUI() override {
        if (highlighted_) map_.clearHighlights();
    }

    std::vector<std::pair<std::string, std::string>> hints() const override {
        return {{"\xE2\x97\x80\xE2\x96\xB6", "Change"}, {"Y", "Suggest"}, {"START", "Send"}, {"B", "Cancel"}};
    }
    bool stepsWithKeys() const override { return true; }

    bool update(const Input& in, uint32_t) override {
        if (in.pressed(Btn::Up)) moveCursor(-1, false);
        if (in.pressed(Btn::Down)) moveCursor(1, false);
        if (in.pressed(Btn::L1)) for (int k = 0; k < 5; ++k) moveCursor(-1, false);
        if (in.pressed(Btn::R1)) for (int k = 0; k < 5; ++k) moveCursor(1, false);
        if (cursor_ >= 0 && cursor_ < int(rows_.size()) && !rows_[size_t(cursor_)].header) {
            int step = 0;
            if (in.pressed(Btn::Right) || in.pressed(Btn::A)) step = 1;
            if (in.pressed(Btn::Left)) step = -1;
            if (step && rows_[size_t(cursor_)].change) {
                rows_[size_t(cursor_)].change(step, in.pressed(Btn::A));
                std::string key = rows_[size_t(cursor_)].key;
                rebuild();
                keepCursor(key);
                changed();
            }
        }
        if (in.pressed(Btn::Y)) {
            Deal s;
            if (aiSuggestDeal(g_, me_, them_, counter_ ? &base_ : nullptr, s)) {
                give_ = SideSt();
                get_ = SideSt();
                for (Slot& sl : slots_) sl = Slot();
                threat_ = -1;
                load(s);
                rebuild();
                keepCursor(rows_.empty() ? "" : rows_[size_t(cursor_)].key);
                changed();
            } else {
                setError("No suggestion: nothing obvious to offer " + playerName(g_, them_) + " right now.");
            }
        }
        if (in.pressed(Btn::B)) {
            answer_ = Answer();  // empty = cancelled
            return true;
        }
        if (in.pressed(Btn::Start)) {
            answer_ = Answer();
            answer_.data = encodeDeal(current());
            std::string e = d_.validate ? d_.validate(answer_) : "";
            if (e.empty()) return true;
            setError(e);
        }
        return false;
    }

    void draw(Canvas& c, uint32_t) override {
        fullPanel(c);
        const Rect r = kFull;
        factionDisc(c, g_, me_, float(r.x + 26), float(r.y + 26), 18.f);
        drawText(c, FONT_LG, r.x + 52, r.y + 6, fitText(FONT_LG, d_.prompt, 330), theme::gold);
        drawText(c, FONT_XS, r.x + 52, r.y + 32, fitText(FONT_XS, d_.context, 340), theme::dim);
        // rows on the left
        const int lx = r.x + 6, lw = 392;
        const int top = r.y + 52, rowH = 21;
        const int bottom = r.y + r.h - 8 - ((error_.empty() && live_.empty()) ? 0 : 34);
        const int visible = std::max(1, (bottom - top) / rowH);
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        // keep a section header visible above the cursor where possible
        int y = top;
        for (int i = scroll_; i < int(rows_.size()) && i < scroll_ + visible; ++i) {
            const DRow& row = rows_[size_t(i)];
            if (row.header) {
                int hx = lx + 6;
                if (row.player >= 0) {
                    drawFactionIcon(c, g_.players[row.player].faction, hx, y + 2, 16);
                    hx += 20;
                }
                drawText(c, FONT_SMB, hx, y + 3, fitText(FONT_SMB, row.label, lw - 30), row.color ? row.color : theme::cyan);
                if (!row.value.empty())
                    drawTextAligned(c, FONT_XS, lx, y + 5, lw - 6, Align::Right, fitText(FONT_XS, row.value, 200), theme::faint);
                y += rowH;
                continue;
            }
            bool sel = i == cursor_;
            hits::add(Rect{lx + 2, y, lw - 4, rowH - 2}, [this, i]() { cursor_ = i; });  // 0.43.0 (P12): rows and arrows take the mouse
            if (sel) c.fillRoundRect(lx + 2, y, lw - 4, rowH - 2, 5, theme::panelHi, 245);
            int x = lx + 12 + row.indent * 14;
            if (row.icon && icons::draw(c, row.icon, x - 2, y + 2, 15)) x += 18;
            drawText(c, FONT_SM, x, y + 2, fitText(FONT_SM, row.label, lx + 190 - x), sel ? theme::text : rgb(190, 196, 212));
            const int vx = lx + 196, vw = lw - 196 - 22;
            uint32_t vc = row.color                                                   ? row.color  // 0.43.0: a warning (red)
                          : row.value == "0" || row.value == "None" || row.value == "-" ? theme::faint
                                                                                        : theme::gold;
            drawTextAligned(c, FONT_SMB, vx, y + 2, vw, Align::Center, fitText(FONT_SMB, row.value, vw - 4), vc);
            if (sel) {
                drawText(c, FONT_SM, vx - 6, y + 2, "\xE2\x97\x80", theme::dim);
                drawText(c, FONT_SM, vx + vw, y + 2, "\xE2\x96\xB6", theme::dim);
                hits::addButton(Rect{vx - 9, y, 16, rowH - 2}, Btn::Left);
                hits::addButton(Rect{vx + vw - 3, y, 16, rowH - 2}, Btn::Right);
            }
            y += rowH;
        }
        if (scroll_ > 0) drawText(c, FONT_SM, lx + lw - 16, top - 12, "\xE2\x96\xB2", theme::dim);
        if (scroll_ + visible < int(rows_.size())) drawText(c, FONT_SM, lx + lw - 16, bottom - 12, "\xE2\x96\xBC", theme::dim);
        // the deal so far, on the right
        const Rect s{r.x + 404, r.y + 48, r.w - 410, r.h - 54};
        drawPanel(c, s, theme::panel, theme::border, 8);
        factionDisc(c, g_, them_, float(s.x + 24), float(s.y + 24), 17.f);
        drawText(c, FONT_SMB, s.x + 48, s.y + 8, fitText(FONT_SMB, playerName(g_, them_), s.w - 54), pcol(g_, them_));
        {
            const Player& tp = g_.players[them_];
            std::string nums = std::to_string(tp.vp) + " VP";
            drawText(c, FONT_XS, s.x + 48, s.y + 26, nums, theme::gold);
            int ix = s.x + 48 + textWidth(FONT_XS, nums) + 8;
            if (icons::draw(c, "trade_good", ix, s.y + 25, 13)) ix += 15;
            drawText(c, FONT_XS, ix, s.y + 26, std::to_string(tp.tradeGoods), theme::text);
            ix += 18;
            if (icons::draw(c, "commodity", ix, s.y + 25, 13)) ix += 15;
            drawText(c, FONT_XS, ix, s.y + 26, std::to_string(tp.commodities), theme::text);
        }
        int yy = s.y + 50;
        const int sx = s.x + 8, sw = s.w - 16, sb = s.y + s.h - 50;
        Deal cur = current();
        auto block = [&](const std::string& title, const std::string& text, uint32_t col) {
            if (yy + 14 > sb) return;
            drawText(c, FONT_XS, sx, yy, title, theme::dim);
            yy += 13;
            for (const std::string& l : wrapMax(FONT_XS, text, sw, 4)) {
                if (yy + 13 > sb) break;
                drawText(c, FONT_XS, sx + 4, yy, l, col);
                yy += 13;
            }
            yy += 4;
        };
        block("You give", sideText(g_, cur.give), theme::text);
        block(playerName(g_, them_) + " gives", sideText(g_, cur.get), pcol(g_, them_));
        for (const Promise& p0 : cur.promises) {
            Promise p = p0;
            p.to = int8_t(p.by == me_ ? them_ : me_);
            block(p.by == me_ ? "You promise" : playerName(g_, them_) + " promises", promiseText(g_, p, false),
                  p.by == me_ ? theme::text : pcol(g_, them_));
        }
        if (cur.threat >= 0) block("If they refuse", "you may attack " + sysLabel(g_, cur.threat), rgb(240, 110, 110));
        // 0.30.0 (owner): the highlighted promissory note / action card, read in full
        if (cursor_ >= 0 && cursor_ < int(rows_.size()) && !rows_[size_t(cursor_)].cardText.empty()) {
            const DRow& row = rows_[size_t(cursor_)];
            const int boxTop = std::max(yy + 4, sb - 112);
            if (boxTop + 30 < sb) {
                c.hline(s.x + 8, s.x + s.w - 8, boxTop, theme::border);
                int ty = boxTop + 4;
                drawText(c, FONT_SMB, sx, ty, fitText(FONT_SMB, row.value, sw), theme::gold);
                ty += 16;
                for (const std::string& l : wrapMax(FONT_XS, row.cardText, sw, 7)) {
                    if (ty + 13 > sb) break;
                    drawText(c, FONT_XS, sx, ty, l, theme::text);
                    ty += 13;
                }
            }
        }
        // will they take it?
        if (!cur.empty() && live_.empty()) {
            float m = aiDealMargin(g_, them_, cur);
            std::string t;
            uint32_t col;
            if (m >= 0.f) {
                t = "Likely to accept";
                col = theme::green;
            } else if (m >= -2.f) {
                t = "May ask for more";
                col = rgb(240, 200, 90);
            } else {
                t = "Likely to refuse";
                col = theme::red;
            }
            c.hline(s.x + 8, s.x + s.w - 8, s.y + s.h - 44, theme::border);
            drawText(c, FONT_XS, sx, s.y + s.h - 38, playerName(g_, them_) + "'s view:", theme::dim);
            drawText(c, FONT_SMB, sx, s.y + s.h - 24, t, col);
        }
        if (!error_.empty()) errorBox(c, error_, lx, r.y + r.h - 6, lw, false);
        else errorBox(c, live_, lx, r.y + r.h - 6, lw, true);
    }

private:
    struct SideSt {
        int tg = 0, comm = 0, note = -1, planet = -1, cap = 0;
        int debt = 0;  // Discordant Stars (Binding Debts): control tokens onto the Vaden's sheet
        int hire = 0;  // Discordant Stars (Hired Guns, 0.43.0): ships hired out
        std::map<int, int> orders;  // Discordant Stars (Arms Dealers): Axis Order cards given (1) or not
        std::map<int, int> frags;
        int ac[2] = {-1, -1};
        std::map<int, int> relics, secrets;  // Thunder's Edge (0.34.0): chosen (1) or not
        std::map<int, int> techs;            // Age of Commerce (galactic event, 0.36.0): shared (1) or not
        std::map<int, int> laws;             // Encrypted Trade Hub (fixes 28): agendas held, given (1) or not
    };
    struct Slot {
        PromiseKind kind = PromiseKind::None;
        bool mine = false;
        int sys = -1, out = -1, amount = 1, rounds = 1;
    };
    struct DRow {
        std::string key, label, value;
        const char* icon = nullptr;
        bool header = false;
        int indent = 0, player = -1;
        uint32_t color = 0;
        std::function<void(int, bool)> change;  // (step, pressed A)
        std::string cardText;                   // 0.30.0 (owner): the chosen card's text, shown in the right pane
    };

    // ---------------------------------------------------------- state <-> deal
    std::vector<int> capturedOf(int holder, int owner) const {
        std::vector<int> v;
        for (int16_t c : g_.players[holder].captured)
            if (c / 16 == owner) v.push_back(c);
        return v;
    }
    DealSide toSide(const SideSt& s, int who, int other) const {
        DealSide d;
        d.tg = s.tg;
        d.comm = s.comm;
        d.note = s.note;
        d.planet = s.planet;
        for (auto& f : s.frags)
            for (int k = 0; k < f.second; ++k) d.fragments.push_back(f.first);
        for (int a : s.ac)
            if (a >= 0) d.actionCards.push_back(a);
        std::vector<int> cap = capturedOf(who, other);
        for (int k = 0; k < s.cap && k < int(cap.size()); ++k) d.captured.push_back(cap[size_t(k)]);
        for (auto& r : s.relics)
            if (r.second) d.relics.push_back(r.first);
        for (auto& o : s.secrets)
            if (o.second) d.secrets.push_back(o.first);
        for (auto& t : s.techs)
            if (t.second) d.techs.push_back(t.first);
        d.debt = s.debt;
        for (auto& o : s.orders)
            if (o.second) d.orders.push_back(o.first);
        d.hire = s.hire;
        for (auto& l : s.laws)
            if (l.second) d.laws.push_back(l.first);
        return d;
    }
    SideSt fromSide(const DealSide& d) const {
        SideSt s;
        s.tg = d.tg;
        s.comm = d.comm;
        s.note = d.note;
        s.planet = d.planet;
        for (int f : d.fragments) ++s.frags[f];
        for (size_t i = 0; i < d.actionCards.size() && i < 2; ++i) s.ac[i] = d.actionCards[i];
        s.cap = int(d.captured.size());
        for (int r : d.relics) s.relics[r] = 1;
        for (int o : d.secrets) s.secrets[o] = 1;
        for (int t : d.techs) s.techs[t] = 1;
        s.debt = d.debt;
        for (int o : d.orders) s.orders[o] = 1;
        s.hire = d.hire;
        for (int l : d.laws) s.laws[l] = 1;
        return s;
    }
    void load(const Deal& d) {
        give_ = fromSide(d.give);
        get_ = fromSide(d.get);
        int i = 0;
        for (const Promise& p : d.promises) {
            if (i >= 3) break;
            Slot& s = slots_[i++];
            s.kind = p.kind;
            s.mine = p.by == me_;
            if (p.kind == PromiseKind::VoteFor) s.out = p.target;
            else s.sys = p.target;
            s.amount = std::max<int>(1, p.amount);
            s.rounds = std::max<int>(1, std::min<int>(3, p.rounds));
        }
        threat_ = d.threat;
    }
    Deal current() const {
        Deal d;
        d.from = me_;
        d.to = them_;
        d.give = toSide(give_, me_, them_);
        d.get = toSide(get_, them_, me_);
        for (const Slot& s : slots_) {
            if (s.kind == PromiseKind::None) continue;
            Promise p;
            p.kind = s.kind;
            p.by = int8_t(s.mine ? me_ : them_);
            p.target = int16_t(s.kind == PromiseKind::VoteFor ? s.out : promiseHasSystem(s.kind) ? s.sys : -1);
            p.amount = int8_t(s.kind == PromiseKind::Repay ? s.amount : 0);
            p.rounds = int8_t(promiseHasRounds(s.kind) ? s.rounds : 1);
            p.agenda = int16_t(s.kind == PromiseKind::VoteFor ? d_.agenda : -1);
            d.promises.push_back(p);
        }
        d.threat = threat_;
        return d;
    }

    // ---------------------------------------------------------- choices
    static void cycle(const std::vector<int>& choices, int& cur, int step) {
        if (choices.empty()) return;
        int i = int(std::find(choices.begin(), choices.end(), cur) - choices.begin());
        if (i >= int(choices.size())) i = 0;
        else i = ((i + (step > 0 ? 1 : -1)) % int(choices.size()) + int(choices.size())) % int(choices.size());
        cur = choices[size_t(i)];
    }
    static void count(int& v, int step, int max, bool wrapA) {
        int nv = std::max(0, std::min(max, v + step));
        if (wrapA && v == max) nv = 0;
        v = nv;
    }
    std::vector<PromiseKind> kindChoices() const {
        std::vector<PromiseKind> k = {PromiseKind::None, PromiseKind::NoAttack, PromiseKind::AvoidSystem, PromiseKind::LetPass,
                                      PromiseKind::AttackSystem, PromiseKind::Repay};
        if (agentLendable(g_, me_) || agentLendable(g_, them_)) k.push_back(PromiseKind::UseAgent);
        if (ownsTechKey(g_, me_, "dsrohdy") || ownsTechKey(g_, them_, "dsrohdy")) k.push_back(PromiseKind::Contract);  // 0.43.0 (D46)
        bool leave = false;
        for (int p : {me_, them_})
            for (int s : {g_.mecatol(), int(g_.nexus)})
                if (leaveRequestable(g_, p, s)) leave = true;
        if (leave) k.push_back(PromiseKind::LeaveSystem);
        if (d_.dealFlags & kDealVote) k.push_back(PromiseKind::VoteFor);
        return k;
    }
    void setDefaults(Slot& s) {
        switch (s.kind) {
            case PromiseKind::Repay: s.mine = get_.tg > 0 || give_.tg == 0; break;
            case PromiseKind::UseAgent: s.mine = !agentLendable(g_, them_); break;
            case PromiseKind::Contract: s.mine = ownsTechKey(g_, me_, "dsrohdy"); break;
            case PromiseKind::LeaveSystem: {
                bool theyCan = !promiseSystems(g_, PromiseKind::LeaveSystem, them_, me_).empty();
                s.mine = !theyCan;
                break;
            }
            default: s.mine = false; break;
        }
        resetTargets(s);
    }
    void resetTargets(Slot& s) {
        int by = s.mine ? me_ : them_, to = s.mine ? them_ : me_;
        auto sys = promiseSystems(g_, s.kind, by, to);
        s.sys = sys.empty() ? -1 : sys[0];
        auto outs = voteOutcomes(g_, d_.agenda);
        s.out = outs.empty() ? -1 : outs[0].first;
        if (s.kind == PromiseKind::Repay) s.amount = std::max(1, (s.mine ? get_.tg : give_.tg) + 1);
        s.rounds = s.kind == PromiseKind::Repay ? 2 : 1;
    }

    // ---------------------------------------------------------- rows
    void sideRows(SideSt& st, int who, int other, bool giving) {
        const Player& pl = g_.players[who];
        const std::string pre = giving ? "give." : "get.";
        if (!components_) return;
        {
            DRow r;
            r.key = pre + "tg";
            r.label = "Trade goods";
            r.icon = "trade_good";
            r.value = std::to_string(st.tg);
            int max = pl.tradeGoods;
            r.change = [&st, max](int step, bool a) { count(st.tg, step, max, a); };
            rows_.push_back(r);
        }
        if (pl.commodities > 0 || st.comm > 0) {
            DRow r;
            r.key = pre + "comm";
            r.label = "Commodities";
            r.icon = "commodity";
            r.value = std::to_string(st.comm);
            int max = pl.commodities;
            r.change = [&st, max](int step, bool a) { count(st.comm, step, max, a); };
            rows_.push_back(r);
        }
        {
            std::vector<int> notes = {-1};
            if (giving)
                for (int16_t n : pl.notes) notes.push_back(n);
            else
                for (int n : askableNotes(g_, who)) notes.push_back(n);
            if (std::find(notes.begin(), notes.end(), st.note) == notes.end()) notes.push_back(st.note);
            if (notes.size() > 1) {
                DRow r;
                r.key = pre + "note";
                r.label = "Promissory note";
                r.icon = "promissory";
                r.value = st.note >= 0 ? noteNameOf(g_, st.note) : "None";
                if (st.note >= 0) r.cardText = noteTextOf(g_, st.note);
                if (branchNoSpot(g_, st.note, other)) {  // 0.43.0 (D119)
                    r.value += " (no planet for it!)";
                    r.color = theme::red;
                    r.cardText += "\n\nWarning: " + playerName(g_, other) +
                                  " has no planet for a Branch Office (not a home planet or Mecatol Rex, without one already) - it would go straight back to the Veldyr.";
                }
                r.change = [&st, notes](int step, bool) { cycle(notes, st.note, step); };
                rows_.push_back(r);
            }
        }
        std::map<int, int> have;
        for (int16_t f : pl.fragments) ++have[f];
        for (auto& f : have) {
            DRow r;
            r.key = pre + "frag." + std::to_string(f.first);
            r.label = kExploreCards[f.first].name;
            r.icon = fragmentIcon(f.first);
            int& v = st.frags[f.first];
            r.value = std::to_string(v);
            int max = f.second;
            r.change = [&v, max](int step, bool a) { count(v, step, max, a); };
            rows_.push_back(r);
        }
        if ((d_.dealFlags & kDealActionCards) && giving && !pl.actionCards.empty()) {
            for (int k = 0; k < 2; ++k) {
                std::vector<int> cards = {-1};
                for (int16_t cd : pl.actionCards) {
                    int copies = int(std::count(pl.actionCards.begin(), pl.actionCards.end(), cd));
                    int used = (st.ac[1 - k] == cd) ? 1 : 0;
                    if (copies > used && std::find(cards.begin(), cards.end(), int(cd)) == cards.end()) cards.push_back(cd);
                }
                if (k == 1 && st.ac[0] < 0 && st.ac[1] < 0) break;
                DRow r;
                r.key = pre + "ac." + std::to_string(k);
                r.label = (d_.dealFlags & kDealBlackMarket) ? "Action card" : "Action card (Arbiters)";
                r.icon = "action_card";
                r.value = st.ac[k] >= 0 ? kActionCards[st.ac[k]].name : "None";
                if (st.ac[k] >= 0) r.cardText = actionCardText(st.ac[k]);
                int* slot = &st.ac[k];
                r.change = [slot, cards](int step, bool) { cycle(cards, *slot, step); };
                rows_.push_back(r);
            }
        }
        // Thunder's Edge: The Silver Flame always, every relic and (giving) secret objectives with Black Market Dealings
        for (int16_t rl : pl.relics) {
            if (!(d_.dealFlags & (kDealBlackMarket | kDealHub)) && std::strcmp(kRelics[rl].key, "thesilverflame")) continue;  // (Encrypted Trade Hub)
            DRow r;
            r.key = pre + "relic." + std::to_string(rl);
            r.label = kRelics[rl].name;
            r.icon = "relic";
            int& v = st.relics[rl];
            r.value = v ? "Yes" : "No";
            r.cardText = relicText(rl);
            r.change = [&v](int, bool) { v = 1 - v; };
            rows_.push_back(r);
        }
        if (eventOn(g_, kEvCommerce))  // Age of Commerce: share a technology (the giver keeps it)
            for (int16_t t : pl.techs) {
                if (kTechs[t].faction >= 0 || g_.players[size_t(other)].hasTech(t)) continue;
                DRow r;
                r.key = pre + "tech." + std::to_string(t);
                r.label = std::string("Share ") + kTechs[t].name;
                r.icon = "cybernetic";
                int& v = st.techs[t];
                r.value = v ? "Yes" : "No";
                r.cardText = techSummary(t);
                r.change = [&v](int, bool) { v = 1 - v; };
                rows_.push_back(r);
            }
        if ((d_.dealFlags & kDealHub) && g_.fixes >= 28)  // Encrypted Trade Hub (fixes 28): 1 relic or agenda - a law held
            for (int a : lawsHeldBy(g_, who)) {
                DRow r;
                r.key = pre + "law." + std::to_string(a);
                r.label = kAgendas[a].name;  // (the agenda icon marks it)
                r.icon = "agenda";
                int& v = st.laws[a];
                r.value = v ? "Yes" : "No";
                r.cardText = agendaText(a, false);
                r.change = [&v](int, bool) { v = 1 - v; };
                rows_.push_back(r);
            }
        if ((d_.dealFlags & kDealBlackMarket) && giving)
            for (int16_t o : pl.secrets) {
                DRow r;
                r.key = pre + "secret." + std::to_string(o);
                r.label = kObjectives[o].name;
                r.icon = "secret";
                int& v = st.secrets[o];
                r.value = v ? "Yes" : "No";
                r.cardText = objectiveSummary(o);
                r.change = [&v](int, bool) { v = 1 - v; };
                rows_.push_back(r);
            }
        if (isFaction(g_, who, "hacan") && mechAbilities(g_)) {
            std::vector<int> planets = {-1};
            for (int x : controlledPlanets(g_, who))
                if (g_.planets[x].units[who].count(UnitType::Mech) && !isHomePlanet(g_, x)) planets.push_back(x);
            if (planets.size() > 1) {
                DRow r;
                r.key = pre + "planet";
                r.label = "Planet (Pride of Kenara)";
                r.icon = "legendary";
                r.value = st.planet >= 0 ? kPlanets[g_.planets[st.planet].def].name : "None";
                r.change = [&st, planets](int step, bool) { cycle(planets, st.planet, step); };
                rows_.push_back(r);
            }
        }
        int cap = int(capturedOf(who, other).size());
        if (cap > 0) {
            DRow r;
            r.key = pre + "cap";
            r.label = giving ? "Return captured units" : "Your captured units back";
            r.value = std::to_string(st.cap);
            r.change = [&st, cap](int step, bool a) { count(st.cap, step, cap, a); };
            rows_.push_back(r);
        }
        if (g_.modules & kModDiscordant)  // Arms Dealers: Axis Orders in this player's play area
            for (int i = 0; i < 8; ++i) {
                if (g_.dsOrders[i] != who || ((g_.dsOrderEx >> i) & 1)) continue;
                DRow r;
                r.key = pre + "order." + std::to_string(i);
                r.label = axisOrderName(i);
                r.icon = "faction/axis";
                int& v = st.orders[i];
                r.value = v ? "Yes" : "No";
                r.cardText = axisOrderText(i);
                r.change = [&v](int, bool) { v = 1 - v; };
                rows_.push_back(r);
            }
        if ((d_.dealFlags & kDealHire) && isFaction(g_, who, "nokar")) {  // Hired Guns (0.43.0): right after an activation
            DRow r;
            r.key = pre + "hire";
            r.label = giving ? "Hired Guns: ships you hire out" : "Hired Guns: their ships for you";
            r.icon = "faction/nokar";
            r.value = std::to_string(st.hire);
            r.cardText = "Hired Guns: after a system is activated the Nokar choose up to 3 of their ships (in it or able to reach it); "
                         "they fight as the active player's units, who may replace each with one of their own at the end of the action.";
            const int max = std::max(0, std::min(3, d_.value));
            r.change = [&st, max](int step, bool a) { count(st.hire, step, max, a); };
            rows_.push_back(r);
        }
        if ((g_.modules & kModDiscordant) && isFaction(g_, other, "vaden") && g_.dsDebt[who] < vadenDebtCap(g_)) {  // Binding Debts
            DRow r;
            r.key = pre + "debt";
            r.label = giving ? "Control tokens on their sheet" : "Their control tokens on your sheet";
            r.icon = "faction/vaden";
            r.value = std::to_string(st.debt);
            r.cardText = "Binding Debts: the Vaden Banking Clans can cash these in later (Collateralized Loans, their mech, their hero).";
            const int max = std::min(9, vadenDebtCap(g_) - g_.dsDebt[who]);  // (fixes 29: no cap - 9 at a time)
            r.change = [&st, max](int step, bool a) { count(st.debt, step, max, a); };
            rows_.push_back(r);
        }
    }

    void promiseRows(int i) {
        Slot& s = slots_[i];
        const std::string pre = "prom." + std::to_string(i) + ".";
        {
            DRow r;
            r.key = pre + "kind";
            r.label = "Promise " + std::to_string(i + 1);
            r.value = s.kind == PromiseKind::None ? "None" : promiseKindName(s.kind);
            auto kinds = kindChoices();
            r.change = [this, &s, kinds](int step, bool) {
                int k = int(std::find(kinds.begin(), kinds.end(), s.kind) - kinds.begin());
                int n = int(kinds.size());
                k = k >= n ? 0 : ((k + (step > 0 ? 1 : -1)) % n + n) % n;
                s.kind = kinds[size_t(k)];
                if (s.kind != PromiseKind::None) setDefaults(s);
            };
            rows_.push_back(r);
        }
        if (s.kind == PromiseKind::None) return;
        {
            DRow r;
            r.key = pre + "by";
            r.label = "Who promises";
            r.indent = 1;
            r.value = s.mine ? "You" : playerName(g_, them_);
            r.change = [this, &s](int, bool) {
                s.mine = !s.mine;
                resetTargets(s);
            };
            rows_.push_back(r);
        }
        const int by = s.mine ? me_ : them_, to = s.mine ? them_ : me_;
        if (s.kind == PromiseKind::UseAgent)  // 0.43.0 (D102): the agent's card beside the promise
            for (size_t k = rows_.size() >= 2 ? rows_.size() - 2 : 0; k < rows_.size(); ++k) rows_[k].cardText = agentCardText(g_, by);
        if (promiseHasSystem(s.kind)) {
            std::vector<int> sys = promiseSystems(g_, s.kind, by, to);
            DRow r;
            r.key = pre + "sys";
            r.label = "System";
            r.indent = 1;
            r.value = sys.empty() ? "none available" : sysLabel(g_, s.sys);
            r.change = [&s, sys](int step, bool) { cycle(sys, s.sys, step); };
            rows_.push_back(r);
        }
        if (s.kind == PromiseKind::VoteFor) {
            std::vector<int> ids;
            std::string name = "-";
            for (const auto& o : voteOutcomes(g_, d_.agenda)) {
                ids.push_back(o.first);
                if (o.first == s.out) name = o.second;
            }
            DRow r;
            r.key = pre + "out";
            r.label = "Vote for";
            r.indent = 1;
            r.value = name;
            r.change = [&s, ids](int step, bool) { cycle(ids, s.out, step); };
            rows_.push_back(r);
        }
        if (s.kind == PromiseKind::Repay) {
            DRow r;
            r.key = pre + "amt";
            r.label = "Trade goods to pay back";
            r.indent = 1;
            r.value = std::to_string(s.amount);
            r.change = [&s](int step, bool a) {
                int nv = std::max(1, std::min(20, s.amount + step));
                if (a && s.amount == 20) nv = 1;
                s.amount = nv;
            };
            rows_.push_back(r);
        }
        if (promiseHasRounds(s.kind)) {
            DRow r;
            r.key = pre + "rnd";
            r.label = s.kind == PromiseKind::Repay ? "Pay back within" : "For";
            r.indent = 1;
            r.value = std::to_string(s.rounds) + (s.rounds == 1 ? " round" : " rounds");
            r.change = [&s](int step, bool) { s.rounds = ((s.rounds - 1 + (step > 0 ? 1 : 2)) % 3) + 1; };
            rows_.push_back(r);
        }
    }

    void rebuild() {
        rows_.clear();
        auto header = [&](const std::string& key, const std::string& label, int player, uint32_t color, const std::string& note = "") {
            DRow r;
            r.key = key;
            r.label = label;
            r.header = true;
            r.player = player;
            r.color = color;
            r.value = note;
            rows_.push_back(r);
        };
        if (components_) {
            header("h.give", "You give", me_, theme::text);
            sideRows(give_, me_, them_, true);
            header("h.get", "You ask " + playerName(g_, them_) + " for", them_, pcol(g_, them_));
            sideRows(get_, them_, me_, false);
        } else {
            header("h.none", "No trading with " + playerName(g_, them_) + " now", -1, theme::faint, "promises only");
        }
        header("h.prom", "Promises", -1, theme::cyan, "not binding");
        for (int i = 0; i < 3; ++i) {
            if (i > 0 && slots_[i - 1].kind == PromiseKind::None && slots_[i].kind == PromiseKind::None) break;
            promiseRows(i);
        }
        if (!counter_ && g_.phase == Phase::Action) {
            std::vector<int> sys = {-1};
            for (int s : threatSystems(g_, me_, them_)) sys.push_back(s);
            if (sys.size() > 1 || threat_ >= 0) {
                header("h.threat", "If they refuse", -1, rgb(240, 110, 110));
                DRow r;
                r.key = "threat";
                r.label = "Threaten to attack";
                r.value = threat_ >= 0 ? sysLabel(g_, threat_) : "None";
                r.change = [this, sys](int step, bool) { cycle(sys, threat_, step); };
                rows_.push_back(r);
            }
        }
        // the systems named in the deal light up on the map (R2 to look)
        std::vector<int> hl;
        for (const Slot& s : slots_)
            if (s.kind != PromiseKind::None && promiseHasSystem(s.kind) && s.sys >= 0) hl.push_back(s.sys);
        if (threat_ >= 0) hl.push_back(threat_);
        if (!hl.empty()) {
            map_.setHighlights(hl);
            highlighted_ = true;
        } else if (highlighted_) {
            map_.clearHighlights();
            highlighted_ = false;
        }
    }

    void moveCursor(int dir, bool fromStart) {
        int n = int(rows_.size());
        if (n == 0) return;
        if (fromStart) cursor_ = (dir > 0) ? -1 : n;
        for (int k = 0; k < n; ++k) {
            cursor_ += dir;
            if (cursor_ < 0) cursor_ = n - 1;
            if (cursor_ >= n) cursor_ = 0;
            if (!rows_[size_t(cursor_)].header) break;
        }
        // keep the section header above the cursor in view
        if (cursor_ > 0 && rows_[size_t(cursor_ - 1)].header && scroll_ > cursor_ - 1) scroll_ = cursor_ - 1;
        const DRow& r = rows_[size_t(cursor_)];
        if (r.key.find(".sys") != std::string::npos || r.key == "threat") {
            int sys = -1;
            if (r.key == "threat") sys = threat_;
            else sys = slots_[r.key[5] - '0'].sys;
            if (sys >= 0) map_.setCursor(g_, sys);
        }
    }
    void keepCursor(const std::string& key) {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == key) {
                cursor_ = int(i);
                return;
            }
        cursor_ = std::min(cursor_, int(rows_.size()) - 1);
        if (cursor_ >= 0 && rows_[size_t(cursor_)].header) moveCursor(1, false);
    }
    void changed() {
        error_.clear();
        Answer a;
        a.data = encodeDeal(current());
        Deal cur = current();
        live_ = cur.empty() ? "" : (d_.validate ? d_.validate(a) : "");
    }

    MapView& map_;
    int me_ = -1, them_ = -1;
    bool counter_ = false, components_ = false, highlighted_ = false;
    Deal base_;
    SideSt give_, get_;
    Slot slots_[3];
    int threat_ = -1;
    std::vector<DRow> rows_;
    int cursor_ = 0, scroll_ = 0;
    std::string live_;
};

}  // namespace

std::unique_ptr<DecisionUI> makeDealUI(const Game& g, const Decision& d, MapView& map) {
    return std::unique_ptr<DecisionUI>(new DealUI(g, d, map));
}
std::unique_ptr<DecisionUI> makeDealResponseUI(const Game& g, const Decision& d, MapView& map) {
    return std::unique_ptr<DecisionUI>(new DealResponseUI(g, d, map));
}
std::unique_ptr<DecisionUI> makeNegotiateUI(const Game& g, const Decision& d) {
    return std::unique_ptr<DecisionUI>(new NegotiateUI(g, d));
}
