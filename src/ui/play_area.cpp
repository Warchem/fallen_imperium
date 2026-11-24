// Play areas (0.43.0): see play_area.h.
#include "ui/play_area.h"

#include <algorithm>
#include <cmath>

#include "engine/breakthroughs.h"
#include "engine/data.h"
#include "engine/deal.h"
#include "engine/query.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "ui/cards.h"
#include "ui/dice.h"
#include "ui/icons.h"
#include "ui/pcolors.h"
#include "ui/superweapon_ui.h"
#include "ui/theme.h"

using namespace ti;

namespace playarea {

namespace {
uint32_t colourOf(const Game& g, int p) { return p >= 0 && p < g.numPlayers ? pcolours::base(g.players[size_t(p)].color) : theme::neutral; }
UnitType orderUnit(int i) {
    static const UnitType k[4] = {UnitType::Dreadnought, UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer};
    return k[std::clamp(i / 2, 0, 3)];
}
const char* unitArt(int t) {
    switch (UnitType(t)) {
        case UnitType::Dreadnought: return "unit/dreadnought";
        case UnitType::Carrier: return "unit/carrier";
        case UnitType::Cruiser: return "unit/cruiser";
        case UnitType::Destroyer: return "unit/destroyer";
        default: return "unit/fighter";
    }
}
std::string heroName(const Player& pl) {
    for (const LeaderState& ls : pl.leaders)
        if (ls.leader >= 0 && kLeaders[ls.leader].type == kHero) return kLeaders[ls.leader].name;
    return "Hero";
}
}  // namespace

std::vector<Item> of(const Game& g, int p, int viewer) {
    std::vector<Item> out;
    if (p < 0 || p >= g.numPlayers) return out;
    const Player& pl = g.players[size_t(p)];
    // other players' promissory notes face up in front of this player (Alliances, Trade Alliance, ...)
    for (int16_t n : pl.notesInPlay) {
        Item it;
        it.kind = Kind::Note;
        it.name = noteNameOf(g, n);
        const int owner = noteOwner(n);
        it.faction = owner >= 0 && owner < g.numPlayers ? g.players[size_t(owner)].faction : -1;
        it.col = colourOf(g, owner);
        it.detail = "Promissory note from " + (owner >= 0 && owner < g.numPlayers ? playerName(g, owner) : std::string("?"));
        it.kindLine = it.detail + " - face up in front of " + playerName(g, p);
        it.text = noteTextOf(g, n);
        out.push_back(it);
    }
    // relics (face up in the play area; exhausted ones dimmed)
    for (int16_t r : pl.relics) {
        Item it;
        it.kind = Kind::Relic;
        it.name = kRelics[r].name;
        it.col = rgb(230, 190, 90);
        it.dim = pl.relicExhausted(r);
        it.detail = it.dim ? std::string("Relic (exhausted)") : std::string("Relic");
        it.kindLine = it.dim ? "Relic - exhausted" : "Relic - ready";
        it.text = relicText(r);
        out.push_back(it);
    }
    // (0.84.0) the Belkosea's superweapon cards (exhausted ones dimmed)
    for (int k = 0; k < kSuperweaponCount; ++k) {
        if (!((pl.swCards >> k) & 1)) continue;
        Item it;
        it.kind = Kind::Superweapon;
        it.name = kSuperweapons[k].name;
        it.value = k;
        it.col = superweapon::colour(k);
        it.dim = (pl.swExhausted >> k) & 1;
        const int x = superweaponPlanet(g, k);
        it.detail = std::string("Superweapon") + (x >= 0 ? std::string(" on ") + kPlanets[g.planets[size_t(x)].def].name : std::string()) + (it.dim ? " (exhausted)" : "");
        it.kindLine = it.dim ? "Superweapon card - exhausted" : "Superweapon card - ready";
        it.text = kSuperweapons[k].text;
        out.push_back(it);
    }
    // laws this player was elected to (Minister of War, Political Censure...)
    for (const Law& l : g.laws)
        if (l.owner == p && l.agenda >= 0) {
            Item it;
            it.kind = Kind::Law;
            it.name = kAgendas[l.agenda].name;
            it.col = rgb(110, 170, 255);
            it.detail = "Law - elected";
            it.kindLine = "Law - " + playerName(g, p) + " was elected";
            it.text = agendaText(l.agenda, false);
            out.push_back(it);
        }
    // a hero placed near the board for this round (Coil Overcharge, Krill Drakkon, Starsails, Darktalon...)
    if (pl.heroRound == g.round && g.round > 0) {
        Item it;
        it.kind = Kind::Hero;
        it.name = heroName(pl);
        it.faction = pl.faction;
        it.col = theme::gold;
        it.detail = "Hero in play until the end of this round";
        it.kindLine = "Hero - beside the board until the end of round " + std::to_string(g.round);
        for (const LeaderState& ls : pl.leaders)
            if (ls.leader >= 0 && kLeaders[ls.leader].type == kHero) it.text = leaderText(ls.leader);
        out.push_back(it);
    }
    // Saint Binal (Rhodun hero): the technologies gained - one is kept at the next strategy phase
    if (!pl.dsTempTechs.empty()) {
        Item it;
        it.kind = Kind::Techs;
        it.name = "Saint Binal";
        it.faction = pl.faction;
        it.col = theme::gold;
        it.value = int(pl.dsTempTechs.size());
        for (int16_t t : pl.dsTempTechs) it.detail += (it.detail.empty() ? "" : ", ") + std::string(kTechs[t].name);
        it.detail += " - one is kept at the next strategy phase";
        it.kindLine = "Hero - technologies gained for now; one is kept at the next strategy phase";
        for (int16_t t : pl.dsTempTechs) it.text += std::string(it.text.empty() ? "" : "\n") + kTechs[t].name + ": " + techSummary(t);
        out.push_back(it);
    }
    // Oracle AI (Augurs): the next public objective, on their sheet - only they (and a holder of Read the Fates) see it
    if ((g.modules & kModDiscordant) && pl.dsSheet >= 0 && isFaction(g, p, "augers")) {
        bool sees = viewer == p;
        if (viewer >= 0 && viewer < g.numPlayers) {
            const auto& v = g.players[size_t(viewer)].notesInPlay;
            sees = sees || std::find(v.begin(), v.end(), int16_t(makeNote(p, kNoteFaction))) != v.end();
        }
        Item it;
        it.kind = Kind::Objective;
        it.name = sees ? kObjectives[pl.dsSheet].name : "Oracle AI";
        it.value = sees ? pl.dsSheet : -1;
        it.glow = sees;
        it.col = kObjectives[pl.dsSheet].kind == 1 ? rgb(240, 160, 70) : rgb(110, 170, 255);
        it.faction = pl.faction;
        it.detail = sees ? std::string("Oracle AI: the next public objective revealed") : std::string("Oracle AI: the next public objective, face down");
        it.kindLine = sees ? std::string("Oracle AI - the next public objective (only you can see it)")
                           : std::string("Oracle AI - the next public objective, face down");
        it.text = sees ? objectiveSummary(pl.dsSheet)
                       : "Only the Augurs - and a player holding their faction promissory note - may look at this card. It is revealed as the "
                         "next public objective.";
        out.push_back(it);
    }
    // Policies (Olradin): each card on its + or - side
    if ((g.modules & kModDiscordant) && isFaction(g, p, "olradin")) {
        static const char* kPlus[3] = {"Connect", "Preserve", "Empower"};
        static const char* kMinus[3] = {"Control", "Plunder", "Exploit"};
        static const char* kCard[3] = {"The People", "The Environment", "The Economy"};
        for (int k = 0; k < 3; ++k) {
            const bool minus = (pl.dsPolicy >> k) & 1;
            Item it;
            it.kind = Kind::Policy;
            it.name = minus ? kMinus[k] : kPlus[k];
            it.value = minus ? 1 : 0;
            it.col = minus ? rgb(220, 90, 80) : rgb(90, 200, 120);
            it.faction = pl.faction;
            it.detail = std::string(kCard[k]) + (minus ? " (-): " : " (+): ") + it.name;
            it.kindLine = std::string("Policy - ") + kCard[k] + (minus ? ", on its - side" : ", on its + side");
            it.text = olradinPolicyText(k, minus);
            out.push_back(it);
        }
    }
    // Axis Orders in this player's play area (the Axis: the ones for sale)
    if (g.modules & kModDiscordant) {
        const int ax = playerOfFaction(g, "axis");
        for (int i = 0; i < 8; ++i) {
            if (g.dsOrders[i] != p) continue;
            Item it;
            it.kind = Kind::Order;
            it.name = axisOrderName(i);
            it.unit = int(orderUnit(i));
            it.faction = ax >= 0 ? g.players[size_t(ax)].faction : pl.faction;
            it.col = colourOf(g, ax);
            it.dim = (g.dsOrderEx >> i) & 1;
            it.detail = it.dim ? std::string("used - goes back to the Axis") : p == ax ? std::string("for sale in deals") : axisOrderText(i);
            it.kindLine = std::string("Axis Order") + (it.dim ? " - used, goes back to the Axis" : p == ax ? " - for sale in deals" : " - bought from the Axis");
            it.text = axisOrderText(i);
            out.push_back(it);
        }
    }
    // Prescient Memories (Myko-Mentori): the Omen dice (0 = used until the next strategy phase)
    if ((g.modules & kModDiscordant) && isFaction(g, p, "mykomentori"))
        for (int k = 0; k < 4; ++k) {
            Item it;
            it.kind = Kind::Omen;
            it.name = "Omen die";
            it.value = pl.dsOmen[k];
            it.dim = pl.dsOmen[k] <= 0;
            it.col = it.dim ? rgb(235, 110, 50) : rgb(200, 150, 255);
            it.faction = pl.faction;
            it.detail = it.dim ? std::string("used - back at the next strategy phase") : "shows " + std::to_string(pl.dsOmen[k]);
            it.kindLine = it.dim ? std::string("Omen die - used, rolled again at the next strategy phase") : "Omen die - showing " + std::to_string(pl.dsOmen[k]);
            it.text = "Prescient Memories: you have 4 \"Omen\" dice. At the start of the strategy phase, roll all 4 Omen dice and place them near "
                      "your faction sheet. Divination: before you would roll a die, you may instead return 1 Omen die near your faction sheet to "
                      "your reinforcements to resolve that roll as if it had the result of that die.";
            out.push_back(it);
        }
    // The Sowing (Firmament): trade goods waiting on the card
    if ((g.modules & kModThundersEdge) && (isFaction(g, p, "firmament") || pl.sowing > 0)) {
        Item it;
        it.kind = Kind::Sowing;
        it.name = "The Sowing";
        it.value = pl.sowing;
        it.col = theme::gold;
        it.faction = pl.faction;
        it.detail = std::to_string(pl.sowing) + " trade good" + (pl.sowing == 1 ? "" : "s") + " on the card";
        it.kindLine = "Breakthrough - " + it.detail;
        if (const BreakthroughDef* b = breakthroughOf("firmament")) it.text = b->text;
        out.push_back(it);
    }
    return out;
}

void drawCard(Canvas& c, const Item& it, int x, int y, int w, int h, int alpha, uint32_t now) {
    if (w < 6 || h < 6) return;
    const int a = it.dim && it.kind != Kind::Omen ? alpha * 2 / 5 : alpha;
    const int r = std::max(2, w / 6);
    if (it.glow) {  // a soft pulsing aura: only you see this card
        const float k = 0.5f + 0.5f * std::sin(float(now % 1600u) / 1600.f * 6.2831853f);
        for (int i = 3; i >= 1; --i) c.frameRoundRect(x - i, y - i, w + 2 * i, h + 2 * i, r + i, rgb(120, 220, 255), 1, int(alpha * (0.25f + 0.35f * k) / float(i)));
    }
    if (it.kind == Kind::Omen) {  // a die, not a card
        const float s = float(std::min(w, h)) * 0.9f;
        dice::drawD10(c, float(x) + float(w) / 2.f, float(y) + float(h) / 2.f, s, it.value > 0 ? it.value : 10, false, -1.f, it.faction, alpha);
        if (it.dim) {  // returned: orange-red until the next strategy phase
            c.fillCircle(float(x) + float(w) / 2.f, float(y) + float(h) / 2.f, s * 0.5f, rgb(235, 90, 40), alpha * 150 / 255);
            c.ring(float(x) + float(w) / 2.f, float(y) + float(h) / 2.f, s * 0.5f, 1.5f, rgb(255, 150, 60), alpha);
        }
        return;
    }
    if (it.kind == Kind::Objective) {
        const Rect cr{x, y, w, h};
        if (it.value >= 0) cards::objective(c, it.value, cr, a);
        else cards::objectiveBack(c, it.col == rgb(240, 160, 70) ? 1 : 2, cr, a);
        return;
    }
    c.fillRoundRect(x, y, w, h, r, rgb(16, 18, 30), a * 235 / 255);
    c.frameRoundRect(x, y, w, h, r, it.col, w >= 24 ? 2 : 1, a);
    const int cx = x + w / 2;
    const int big = std::min(w, h) * 3 / 5;
    switch (it.kind) {
        case Kind::Note:
        case Kind::Hero:
            drawFactionIcon(c, it.faction, cx - big / 2, y + (h - big) / 2 - (h >= 30 ? h / 10 : 0), big, a);
            if (it.kind == Kind::Hero) c.frameRoundRect(x + 2, y + 2, w - 4, h - 4, std::max(1, r - 1), theme::gold, 1, a);
            break;
        case Kind::Techs: {  // a dot per technology in its colour
            const int n = std::max(1, it.value), d = std::max(2, w / (n * 2 + 1));
            drawFactionIcon(c, it.faction, cx - big / 3, y + h / 6, big * 2 / 3, a);
            for (int k = 0; k < n; ++k)
                c.fillCircle(float(x + d + k * 2 * d) + float(d) / 2.f + float(w - (n * 2 - 1) * d - 2 * d) / 2.f, float(y + h * 3 / 4), float(d) * 0.6f,
                             theme::gold, a);
            break;
        }
        case Kind::Policy: {
            const FontData& f = h >= 120 ? FONT_XL : h >= 34 ? FONT_LG : FONT_SMB;
            drawTextAligned(c, f, x, y + h / 2 - f.lineHeight / 2 - (h >= 30 ? h / 10 : 0), w, Align::Center, it.value ? "-" : "+", it.col, a);
            break;
        }
        case Kind::Order: {
            icons::drawUnitArt(c, unitArt(it.unit), float(cx), float(y) + float(h) * 0.5f, w * 4 / 5, h / 2, it.col, false, a);
            const int es = std::max(6, w / 3);
            drawFactionIcon(c, it.faction, x + 2, y + 2, es, a);
            break;
        }
        case Kind::Relic: {  // a gem-like relic mark
            const float r = float(big) * 0.42f, gx = float(cx), gy = float(y) + float(h) * (h >= 30 ? 0.4f : 0.5f);
            float xs[6], ys[6];
            for (int k = 0; k < 6; ++k) {
                const float a = 3.14159265f / 3.f * float(k) + 3.14159265f / 6.f;
                xs[k] = gx + std::cos(a) * r;
                ys[k] = gy + std::sin(a) * r;
            }
            c.fillPolygon(xs, ys, 6, rgb(150, 110, 40), a);
            c.fillCircle(gx, gy, r * 0.55f, rgb(255, 225, 140), a);
            break;
        }
        case Kind::Superweapon:  // (0.84.0) the sphere with its iris
            superweapon::drawPiece(c, it.value, float(cx), float(y) + float(h) * (h >= 30 ? 0.42f : 0.5f), float(big) * 0.45f, now, it.dim, alpha);
            break;
        case Kind::Law: {  // the agenda icon, tinted blue
            if (!icons::draw(c, "agenda", cx - big / 2, y + (h - big) / 2 - (h >= 30 ? h / 10 : 0), big, a, rgb(150, 190, 255)))
                c.fillCircle(float(cx), float(y + h / 2), float(big) / 3.f, rgb(110, 170, 255), a);
            break;
        }
        case Kind::Sowing: {
            const int is = big;
            if (!icons::draw(c, "trade_good", cx - is / 2, y + h / 8, is, a)) c.fillCircle(float(cx), float(y + h / 3), float(is) / 3.f, theme::gold, a);
            drawTextAligned(c, h >= 34 ? FONT_SMB : FONT_XS, x, y + h - (h >= 34 ? 16 : 11), w, Align::Center, std::to_string(it.value), theme::gold, a);
            break;
        }
        default: break;
    }
    if (w >= 100 && h >= 120 && it.kind != Kind::Sowing) {  // large (the play-area window): the whole title
        const std::vector<std::string> lines = wrapText(FONT_SMB, it.name, w - 12);
        int ty = y + h - 8 - int(lines.size()) * (FONT_SMB.lineHeight + 1);
        for (const std::string& l : lines) {
            drawTextAligned(c, FONT_SMB, x, ty, w, Align::Center, l, theme::text, a);
            ty += FONT_SMB.lineHeight + 1;
        }
    } else if (h >= 34 && it.kind != Kind::Sowing) {  // the title's first word along the bottom
        std::string t = it.name.substr(0, it.name.find(' '));
        if (t == "The" && it.name.size() > 4) t = it.name.substr(4, it.name.find(' ', 4) == std::string::npos ? std::string::npos : it.name.find(' ', 4) - 4);
        drawTextAligned(c, FONT_XS, x, y + h - 11, w, Align::Center, fitText(FONT_XS, t, w - 2), theme::text, a);
    }
}

void drawCell(Canvas& c, const std::vector<Item>& items, float x, float y, float size, int alpha, uint32_t now) {
    if (items.empty()) return;
    const int n = int(items.size());
    if (size < 34) {  // just how many
        drawTextAligned(c, FONT_XS, int(x - size), int(y) - 6, int(2 * size), Align::Center, std::to_string(n) + " in play", theme::text, alpha);
        return;
    }
    const int shown = std::min(n, size >= 50 ? 6 : 4);
    const int cols = shown <= 2 ? shown : shown <= 4 ? 2 : 3;
    const int rows = (shown + cols - 1) / cols;
    float h = std::min(size * 1.25f / float(rows) - 3.f, size * 0.9f);
    float w = std::min(h / 1.3f, size * 1.45f / float(cols) - 3.f);
    h = std::min(h, w * 1.3f);
    const float gw = float(cols) * (w + 3.f) - 3.f, gh = float(rows) * (h + 3.f) - 3.f;
    for (int k = 0; k < shown; ++k) {
        const int cx = k % cols, cy = k / cols;
        const int inRow = std::min(cols, shown - cy * cols);  // (a short last row is centred)
        const float rowW = float(inRow) * (w + 3.f) - 3.f;
        const float px = x - rowW / 2.f + float(cx) * (w + 3.f), py = y - gh / 2.f + float(cy) * (h + 3.f);
        (void)gw;
        drawCard(c, items[size_t(k)], int(px), int(py), int(w), int(h), alpha, now);
    }
    if (n > shown)
        drawTextAligned(c, FONT_XS, int(x - size), int(y + gh / 2.f), int(2 * size), Align::Center, "+" + std::to_string(n - shown), theme::text, alpha);
}

}  // namespace playarea

// ======================================================================== the play-area window (0.43.4)
#include "core/input.h"
#include "ui/hits.h"
#include "ui/info_screens.h"
#include "ui/widgets.h"

namespace {

class PlayAreaScreen : public InfoScreen {
public:
    PlayAreaScreen(const Game& g, int viewer, int player) : g_(g), viewer_(viewer), p_(player) {}

    bool update(const Input& in) override {
        if (in.pressed(Btn::B) || in.pressed(Btn::A) || in.pressed(Btn::Menu)) return false;
        const int n = int(playarea::of(g_, p_, viewer_).size());
        if (n == 0 && !nextPlayer(1)) return false;  // (everything left play while the window was open)
        if (in.pressed(Btn::Left) || in.pressed(Btn::Up)) sel_ = (sel_ + n - 1) % std::max(1, n);
        if (in.pressed(Btn::Right) || in.pressed(Btn::Down)) sel_ = (sel_ + 1) % std::max(1, n);
        if (in.pressed(Btn::L1)) nextPlayer(-1);
        if (in.pressed(Btn::R1)) nextPlayer(1);
        if (pending_ != 0) {  // a click on an arrow / a player button (hits run while drawing has finished)
            if (pending_ == 1) sel_ = (sel_ + 1) % std::max(1, n);
            else if (pending_ == -1) sel_ = (sel_ + n - 1) % std::max(1, n);
            else if (pending_ == 2) nextPlayer(1);
            else if (pending_ == -2) nextPlayer(-1);
            pending_ = 0;
        }
        return true;
    }

    void draw(Canvas& c, uint32_t now) override {
        const std::vector<playarea::Item> items = playarea::of(g_, p_, viewer_);
        const int n = int(items.size());
        if (n > 0) sel_ = std::clamp(sel_, 0, n - 1);
        c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 160);
        const Rect r{10, 10, 620, 436};
        const uint32_t pc = colourOf(g_, p_);
        drawPanel(c, r, theme::panel, pc, 12);
        hits::addBlock(r);  // clicks inside the window don't close it (outside: A closes; right click: B)
        // title: whose play area, and where we are in it
        int tx = r.x + 14;
        if (p_ >= 0 && p_ < g_.numPlayers) {
            drawFactionIcon(c, g_.players[size_t(p_)].faction, tx, r.y + 7, 28);
            tx += 34;
        }
        const std::string who = p_ >= 0 && p_ < g_.numPlayers ? playerName(g_, p_) + (p_ == viewer_ ? " (you)" : "") : std::string("?");
        drawText(c, FONT_LG, tx, r.y + 10, fitText(FONT_LG, who + " - in play", r.w - 150 - (tx - r.x)), pc);
        if (n > 0) drawTextAligned(c, FONT_MDB, r.x, r.y + 14, r.w - 16, Align::Right, std::to_string(sel_ + 1) + " / " + std::to_string(n), theme::gold);
        if (n == 0) {
            drawTextAligned(c, FONT_MD, r.x, r.y + 180, r.w, Align::Center, "Nothing in play.", theme::dim);
            hints(c);
            return;
        }
        // the strip of every card along the top (click one to read it)
        const int tw = 38, th = 50, gap = 6;
        const int fit = std::max(1, (r.w - 28 + gap) / (tw + gap));
        int first = 0;
        if (n > fit) first = std::clamp(sel_ - fit / 2, 0, n - fit);
        const int shown = std::min(n, fit);
        const int stripW = shown * (tw + gap) - gap;
        const int sx = r.x + (r.w - stripW) / 2, sy = r.y + 46;
        const float pulse = 0.5f + 0.5f * std::sin(float(now % 1500u) / 1500.f * 6.2831853f);
        for (int k = 0; k < shown; ++k) {
            const int i = first + k;
            const int x = sx + k * (tw + gap), y = sy + (i == sel_ ? 0 : 4);
            hits::addAction(Rect{x, sy, tw, th + 4}, [this, i]() { sel_ = i; });  // (a click picks it; it doesn't close)
            drawCard(c, items[size_t(i)], x, y, tw, th, i == sel_ ? 255 : 170, now);
            if (i == sel_) c.frameRoundRect(x - 2, y - 2, tw + 4, th + 4, 8, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
        }
        if (first > 0) drawText(c, FONT_SMB, sx - 14, sy + th / 2 - 6, "\xE2\x97\x80", theme::dim);
        if (first + shown < n) drawText(c, FONT_SMB, sx + stripW + 4, sy + th / 2 - 6, "\xE2\x96\xB6", theme::dim);
        c.fillRect(r.x + 14, sy + th + 12, r.w - 28, 1, theme::border);
        // the card, large, with arrows either side
        const playarea::Item& it = items[size_t(sel_)];
        const int cw = 168, ch = 222, cx = r.x + 46, cy = sy + th + 30;
        drawCard(c, it, cx, cy, cw, ch, 255, now);
        if (n > 1) {
            const int ay = cy + ch / 2 - 14;
            arrow(c, Rect{r.x + 12, ay, 26, 28}, "\xE2\x97\x80", -1);
            arrow(c, Rect{cx + cw + 8, ay, 26, 28}, "\xE2\x96\xB6", 1);
        }
        // its name, what it is and the full text
        const int x = cx + cw + 46, w = r.x + r.w - 16 - x;
        int y = cy;
        for (const std::string& l : wrapText(FONT_LG, it.name, w)) {
            drawText(c, FONT_LG, x, y, l, it.col);
            y += FONT_LG.lineHeight + 2;
        }
        if (!it.kindLine.empty())
            for (const std::string& l : wrapText(FONT_SM, it.kindLine, w)) {
                drawText(c, FONT_SM, x, y, l, theme::dim);
                y += FONT_SM.lineHeight + 1;
            }
        y += 8;
        const int maxY = r.y + r.h - 12;
        const std::string body = it.text.empty() ? it.detail : it.text;
        for (const std::string& l : wrapText(FONT_MD, body, w)) {
            if (y + FONT_MD.lineHeight > maxY) {
                drawText(c, FONT_SM, x, y, "...", theme::dim);
                break;
            }
            drawText(c, FONT_MD, x, y, l, theme::text);
            y += FONT_MD.lineHeight + 2;
        }
        hints(c);
    }

private:
    uint32_t colourOf(const Game& g, int p) const { return p >= 0 && p < g.numPlayers ? pcolours::base(g.players[size_t(p)].color) : theme::neutral; }
    // the next (dir 1) / previous (-1) player with something in play; false if nobody else has
    bool nextPlayer(int dir) {
        for (int k = 1; k <= g_.numPlayers; ++k) {
            const int q = ((p_ + dir * k) % g_.numPlayers + g_.numPlayers) % g_.numPlayers;
            if (q == p_) break;
            if (!playarea::of(g_, q, viewer_).empty()) {
                p_ = q;
                sel_ = 0;
                return true;
            }
        }
        return false;
    }
    int othersWithCards() const {
        int k = 0;
        for (int q = 0; q < g_.numPlayers; ++q)
            if (q != p_ && !playarea::of(g_, q, viewer_).empty()) ++k;
        return k;
    }
    void arrow(Canvas& c, Rect a, const char* glyph, int dir) {
        hits::addAction(a, [this, dir]() { pending_ = dir; });
        c.fillRoundRect(a.x, a.y, a.w, a.h, 6, theme::panelHi, 220);
        drawTextAligned(c, FONT_MDB, a.x, a.y + a.h / 2 - FONT_MDB.lineHeight / 2, a.w, Align::Center, glyph, theme::gold);
    }
    void hints(Canvas& c) {
        std::vector<std::pair<std::string, std::string>> h;
        if (int(playarea::of(g_, p_, viewer_).size()) > 1) h.push_back({"\xE2\x97\x80\xE2\x96\xB6", "Previous / next"});
        if (othersWithCards() > 0) h.push_back({"L1/R1", "Other players"});
        h.push_back({"B", "Close"});
        drawHintBar(c, h);
    }

    const Game& g_;
    int viewer_, p_;
    int sel_ = 0;
    int pending_ = 0;
};

}  // namespace

std::unique_ptr<InfoScreen> makePlayAreaScreen(const Game& g, int viewer, int player) {
    return std::unique_ptr<InfoScreen>(new PlayAreaScreen(g, viewer, player));
}
