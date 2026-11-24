// League mode drafts (0.80.0, owner 2026-11-20 19:26: "Make some drafts of what the league menus will look like"): every
// league screen drawn with sample data - a player who has finished Bronze, Silver, the Lantern Cup and the Ion Storm Circuit
// and is half way through the Gold League (TI4_UITEST_ONLY=leaguedrafts; frames NN 80 ms apart for the moving parts).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/league_ui.h"
#include "engine/data.h"
#include "game/leagues.h"
#include "gfx/canvas.h"
#include "ui/hits.h"
#include "ui/pcolors.h"
#include "ui/emblems.h"
#include "ui/unlock_banner.h"
#include "engine/data.h"

namespace {
float testScale() {  // (as uitest.cpp: TI4_SCALE=2.25 draws as on a 1080p monitor)
    const char* e = std::getenv("TI4_SCALE");
    const float v = e ? float(std::atof(e)) : 1.f;
    Canvas::screenScale() = v > 1.f ? v : 1.f;
    return v > 1.f ? v : 1.f;
}
void savePpm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.pw(), c.ph());
    for (int i = 0; i < c.pw() * c.ph(); ++i) {
        const uint32_t p = c.pixels()[i];
        const unsigned char v[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
        std::fwrite(v, 1, 3, f);
    }
    std::fclose(f);
}
}  // namespace

using namespace leagueui;

namespace {
std::vector<LadderEntry> sampleLadder() {
    std::vector<LadderEntry> v;
    for (const league::LeagueDef* d : league::all()) {
        LadderEntry e;
        e.def = d;
        const std::string id = d->id, after = d->after ? d->after : "";
        if (id == "bronze" || id == "silver" || id == "lantern" || id == "ionstorm") e.state = kFinished;
        else if (id == "gold") e.state = kInProgress, e.game = 5, e.points = 26;
        else if (after == "" || after == "bronze" || after == "silver") e.state = kOpen;
        else e.state = kLocked;
        v.push_back(e);
    }
    return v;
}
int indexOf(const std::vector<LadderEntry>& v, const char* id) {
    for (size_t i = 0; i < v.size(); ++i)
        if (std::string(v[i].def->id) == id) return int(i);
    return 0;
}
}  // namespace

int leagueDraftScreens(const std::string& outDir) {
    const int frames = std::getenv("TI4_LEAGUE_FRAMES") ? std::max(1, std::atoi(std::getenv("TI4_LEAGUE_FRAMES"))) : 1;
    const league::LeagueDef* gold = league::find("gold");
    const league::LeagueDef* silver = league::find("silver");
    std::vector<const league::RivalDef*> gr = league::rivalsOf("gold");
    int n = 0;
    auto shot = [&](const char* name, auto draw) {
        for (int f = 0; f < frames; ++f) {
            const uint32_t now = 2000u + uint32_t(f) * 80u;
            pcolours::setNow(now);
            Canvas c(640, 480, testScale());
            hits::clear();
            drawBackdrop(c, now);
            draw(c, now);
            char buf[96];
            if (frames > 1) std::snprintf(buf, sizeof buf, "/%s_%02d.ppm", name, f);
            else std::snprintf(buf, sizeof buf, "/%s.ppm", name);
            savePpm(c, outDir + buf);
            ++n;
        }
    };
    const std::vector<LadderEntry> ladder = sampleLadder();
    shot("league_ladder_a", [&](Canvas& c, uint32_t now) { drawLadder(c, ladder, indexOf(ladder, "gold"), now); });
    shot("league_ladder_a_locked", [&](Canvas& c, uint32_t now) { drawLadder(c, ladder, indexOf(ladder, "crystal"), now); });
    shot("league_ladder_b", [&](Canvas& c, uint32_t now) { drawLadderMap(c, ladder, indexOf(ladder, "gold"), now); });

    // the Gold League, five games played
    HubView h;
    h.def = gold;
    h.game = 5;
    const int pts[] = {31, 24, 22, 17, 12}, last[] = {0, 2, 3, 4, 5}, mv[] = {0, 1, -1, 0, -1};
    for (size_t i = 0; i < gr.size() && i < 5; ++i) {
        Standing s;
        s.name = gr[i]->name;
        s.faction = ti::findFaction(gr[i]->faction);
        s.arch = gr[i]->arch;
        s.points = pts[i], s.lastPlace = last[i], s.move = mv[i];
        h.table.push_back(s);
    }
    Standing you;
    you.name = "You";
    you.you = true;
    you.faction = ti::findFaction("letnev");
    you.points = 26, you.lastPlace = 1, you.move = 1;
    h.table.push_back(you);
    if (!gr.empty()) {
        h.yourRival = gr[0]->name;
        h.rivalGap = 5;
        h.talker = gr[0]->name;
        h.talkerFaction = ti::findFaction(gr[0]->faction);
        h.quote = gr[0]->taunt;
    }
    shot("league_hub", [&](Canvas& c, uint32_t now) { drawHub(c, h, now); });

    // its sixth game's faction draft: three rivals have picked, you pick fourth
    DraftView dv;
    dv.def = gold;
    dv.game = 5;
    const char* picked[] = {"crimson", "ralnel", "bastion"};
    for (int i = 0; i < 6; ++i) {
        DraftSeat s;
        if (i == 3) s.you = true, s.name = "You";
        else s.name = gr[size_t(i < 3 ? i : i - 1) % gr.size()]->name;
        if (i < 3) s.faction = ti::findFaction(picked[i]);
        dv.order.push_back(s);
    }
    dv.current = 3;
    for (const char* k : {"bastion", "crimson", "deepwrought", "firmament", "ralnel", "hacan", "nekro", "yin", "letnev"})
        dv.offer.push_back(ti::findFaction(k));
    dv.sel = 3;
    shot("league_draft", [&](Canvas& c, uint32_t now) { drawDraft(c, dv, now); });

    // the results of game 5
    ResultView rv;
    rv.def = gold;
    rv.game = 5;
    struct S {
        int who, vp, place, pp, bp, total, move;
    } rs[] = {{0, 10, 0, 6, 1, 31, 0}, {-1, 9, 1, 4, 3, 26, 1}, {1, 8, 2, 3, 0, 24, 1}, {2, 7, 3, 1, 1, 22, -1}, {3, 6, 4, 0, 0, 17, 0}, {4, 3, 5, 0, 0, 12, -1}};
    for (const S& s : rs) {
        ResultRow r;
        if (s.who < 0) r.you = true, r.faction = ti::findFaction("letnev");
        else r.name = gr[size_t(s.who)]->name, r.faction = ti::findFaction(gr[size_t(s.who)]->faction);
        r.vp = s.vp, r.place = s.place, r.placePts = s.pp, r.bonusPts = s.bp, r.total = s.total, r.move = s.move;
        rv.rows.push_back(r);
    }
    const bool got[] = {true, false, true, true, false, false, false, false};
    for (size_t i = 0; i < league::bonuses().size(); ++i) rv.yourBonuses.push_back({league::bonuses()[i].label, i < 8 && got[i]});
    if (gr.size() > 1) {
        rv.speaker = gr[1]->name;
        rv.speakerFaction = ti::findFaction(gr[1]->faction);
        rv.quote = gr[1]->taunt;
        rv.memory = std::string(gr[1]->name) + " will remember this game: you took two of its planets and broke a ceasefire. Grudge: " +
                    gr[1]->grudge;
    }
    shot("league_results", [&](Canvas& c, uint32_t now) { drawResults(c, rv, now); });

    // the Silver League won, and a league lost
    WonView wv;
    wv.def = silver;
    wv.points = 37, wv.games = 8;
    for (const league::LeagueDef* d : league::all())
        if (std::string(d->after ? d->after : "") == "silver") wv.opens.push_back(d);
    shot("league_won", [&](Canvas& c, uint32_t now) { drawWon(c, wv, now); });
    WonView lv;
    lv.def = league::find("ionstorm");
    lv.won = false;
    lv.winner = "Volt Marshal Kesra";
    for (const league::RivalDef* r : league::rivalsOf("ionstorm")) {
        Standing st;
        st.name = r->name, st.faction = ti::findFaction(r->faction), st.arch = r->arch;
        st.points = r == league::rivalsOf("ionstorm")[0] ? 19 : r == league::rivalsOf("ionstorm")[1] ? 14 : 9;
        lv.table.push_back(st);
    }
    {
        Standing st;
        st.you = true, st.faction = ti::findFaction("mentak"), st.points = 16;
        lv.table.push_back(st);
    }
    shot("league_lost", [&](Canvas& c, uint32_t now) { drawWon(c, lv, now); });

    // the Gold League's rivals
    std::vector<RivalCard> cards;
    const int rec[][4] = {{5, 2, 3, 0}, {5, 3, 2, 1}, {5, 4, 1, 0}, {3, 2, 1, 2}, {0, 0, 0, 0}};
    for (size_t i = 0; i < gr.size() && i < 6; ++i) {
        RivalCard k;
        k.def = gr[i];
        if (i < 5) k.games = rec[i][0], k.youAbove = rec[i][1], k.theyAbove = rec[i][2], k.broken = rec[i][3];
        cards.push_back(k);
    }
    shot("league_rivals", [&](Canvas& c, uint32_t now) { drawRivals(c, gold, cards, 1, now); });
    shot("league_emblems", [&](Canvas& c, uint32_t now) { drawEmblemSheet(c, now); });
    // (0.85.2) the Blue Reverie leagues: their cards in the ladder (open - the faction a secret), a win (the faction revealed)
    {
        std::vector<LadderEntry> open = ladder;
        for (LadderEntry& e : open)
            if (std::string(e.def->id) == "gold") e.state = kFinished;
            else if (std::string(e.def->after ? e.def->after : "") == "gold" && e.state == kLocked) e.state = kOpen;
        for (const char* id : {"forge", "arsenal", "necropolis"}) {
            const std::string nm = std::string("league_ladder_br_") + id;
            shot(nm.c_str(), [&](Canvas& c, uint32_t now) { drawLadder(c, open, indexOf(open, id), now); });
        }
        shot("league_ladder_b_br", [&](Canvas& c, uint32_t now) { drawLadderMap(c, open, indexOf(open, "arsenal"), now); });
        if (const league::LeagueDef* ars = league::find("arsenal")) {
            WonView bw;
            bw.def = ars;
            bw.points = 33, bw.games = 7;
            shot("league_won_br", [&](Canvas& c, uint32_t now) { drawWon(c, bw, now); });
        }
    }
    hits::clear();
    std::printf("uitest: %d league draft screens\n", n);
    return n;
}

// 0.81.0 drafts (owner 2026-11-21 09:20): the unlock banner for each kind of unlock, over the Tournament's backdrop, on a 16:9
// screen (TI4_UITEST_ONLY=unlockdrafts; frames TI4_UNLOCK_FRAMES at 80 ms from the start for a GIF; stills at 2.4 s).
int unlockDraftScreens(const std::string& outDir) {
    const float sc = testScale();
    const int W = std::getenv("TI4_UNLOCK_W") ? std::atoi(std::getenv("TI4_UNLOCK_W")) : 854;
    const int frames = std::getenv("TI4_UNLOCK_FRAMES") ? std::max(0, std::atoi(std::getenv("TI4_UNLOCK_FRAMES"))) : 0;
    std::vector<std::pair<const char*, unlockbanner::Unlock>> shots;
    {
        unlockbanner::Unlock u;
        u.kind = unlockbanner::kRival;
        u.category = "RIVAL COMMANDER";
        const league::RivalDef* r = league::rivalsOf("bronze").empty() ? nullptr : league::rivalsOf("bronze")[0];
        u.name = r ? r->name : "Speaker Ollo Mire";
        u.faction = r ? ti::findFaction(r->faction) : ti::findFaction("xxcha");
        u.line = "Now a commander you can seat in a Casual Game.";
        u.source = "Beaten in the Bronze League";
        shots.push_back({"unlock_rival", u});
    }
    {
        unlockbanner::Unlock u;
        u.kind = unlockbanner::kColour;
        u.category = "FACTION COLOUR";
        u.colour = 23;
        u.name = pcolours::info(23).name;
        u.line = "A new colour for your fleets, borders and structures.";
        u.source = "Earned by finishing the Ultimate League";
        shots.push_back({"unlock_colour", u});
    }
    {
        unlockbanner::Unlock u;
        u.kind = unlockbanner::kEmblem;
        u.category = "EMBLEM";
        u.emblem = "gold";
        u.emblemColour = league::find("gold") ? league::find("gold")->color : rgb(240, 200, 80);
        u.name = emblems::name("gold");
        u.line = "Shown under the title menu, for everyone to see.";
        u.source = "Earned by winning the Gold League";
        shots.push_back({"unlock_emblem", u});
    }
    {
        unlockbanner::Unlock u;
        u.kind = unlockbanner::kFaction;
        u.category = "FACTION";
        u.name = "Uydai Conclave";
        u.faction = ti::findFaction("nivyn");  // (draft: a stand-in portrait until the Blue Reverie portraits are drawn)
        u.line = "A Blue Reverie faction, now in the faction picker.";
        u.source = "Earned by winning the Uydai Conclave's league";
        shots.push_back({"unlock_faction", u});
    }
    int n = 0;
    for (const auto& s : shots) {
        for (int f = 0; f <= frames; ++f) {
            const uint32_t t = f < frames ? uint32_t(f) * 80u : 2400u;
            const uint32_t now = 5000u + t;
            pcolours::setNow(now);
            Canvas c(W, 480, sc);
            hits::clear();
            leagueui::drawBackdrop(c, now);
            unlockbanner::draw(c, s.second, t, now);
            char buf[96];
            if (f < frames) std::snprintf(buf, sizeof buf, "/%s_%03d.ppm", s.first, f);
            else std::snprintf(buf, sizeof buf, "/%s.ppm", s.first);
            savePpm(c, outDir + buf);
            ++n;
        }
    }
    hits::clear();
    std::printf("uitest: %d unlock banner drafts\n", n);
    return n;
}
