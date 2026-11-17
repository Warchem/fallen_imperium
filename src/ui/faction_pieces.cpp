#include "ui/faction_pieces.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/breakthroughs.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/cards.h"
#include "ui/dice.h"
#include "ui/icons.h"
#include "ui/info_screens.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ti;

namespace pieces {

namespace {

constexpr float kPi = 3.14159265f;

uint32_t colourOf(const Game& g, int p) {
    if (p == kNeutral || p == kCapturedToken) return theme::neutral;
    return p >= 0 && p < g.numPlayers ? theme::player[g.players[size_t(p)].color % 8] : theme::text;
}
int factionOf(const Game& g, int p) { return p >= 0 && p < g.numPlayers ? g.players[size_t(p)].faction : -1; }
std::string shortOf(const Game& g, int p) { return p >= 0 && p < g.numPlayers ? factionShortName(g.players[size_t(p)].faction) : "?"; }

// a short name for a system: its home's faction, its first planet, or the tile's name
std::string sysLabel(const Game& g, int s) {
    if (s < 0 || s >= int(g.systems.size())) return "?";
    const SystemState& st = g.systems[size_t(s)];
    if (st.home >= 0 && st.home < g.numPlayers) return std::string(shortOf(g, st.home)) + " home";
    if (!st.planets.empty()) return kPlanets[size_t(g.planets[size_t(st.planets[0])].def)].name;
    return st.d().name;
}
std::string planetLabel(const Game& g, int x) {
    return x >= 0 && x < int(g.planets.size()) ? std::string(kPlanets[size_t(g.planets[size_t(x)].def)].name) : std::string("?");
}

// the rule's text by its ability name (faction_text.cpp)
std::string ability(int faction, const char* name) {
    if (faction < 0) return "";
    for (int i = 0; i < 3; ++i)
        if (kFactionInfo[faction].abilities[i] && !std::strcmp(kFactionInfo[faction].abilities[i], name)) return abilityText(faction, i);
    return "";
}

void unitsLine(const Force& f, int sys, const Game& g, Group& gr, const char* where) {
    for (int t = 0; t < kUnitTypes; ++t) {
        if (!f.galv[t]) continue;
        Piece pc;
        pc.kind = Kind::Galvanize;
        pc.count = f.galv[t];
        pc.value = t;
        pc.col = rgb(255, 210, 80);
        pc.label = unitTypeShort(UnitType(t));
        pc.name = std::to_string(f.galv[t]) + " galvanized " + unitTypeName(UnitType(t), f.galv[t] > 1);
        pc.detail = std::string(where) + " - " + sysLabel(g, sys) + ". Each rolls 1 more die in combat.";
        gr.pieces.push_back(pc);
    }
}

}  // namespace

std::vector<Group> of(const Game& g, int p, int viewer) {
    std::vector<Group> out;
    if (p < 0 || p >= g.numPlayers) return out;
    const Player& pl = g.players[size_t(p)];
    const int f = pl.faction;
    const uint32_t mine = colourOf(g, p);
    const bool te = (g.modules & kModThundersEdge) != 0, ds = (g.modules & kModDiscordant) != 0;
    auto is = [&](const char* k) { return isFaction(g, p, k); };

    // ---- Vaden: Fine Print debt - other players' control tokens on the sheet
    if (ds && is("vaden")) {
        Group gr;
        gr.title = "Debt tokens";
        gr.note = ability(f, "Fine Print");
        gr.empty = "Nobody owes the Vaden yet.";
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p || g.dsDebt[q] <= 0) continue;
            Piece pc;
            pc.kind = Kind::Control;
            pc.faction = factionOf(g, q);
            pc.col = colourOf(g, q);
            pc.count = g.dsDebt[q];
            pc.label = shortOf(g, q);
            pc.name = playerName(g, q) + ": " + std::to_string(g.dsDebt[q]) + " control token" + (g.dsDebt[q] == 1 ? "" : "s");
            pc.detail = "On the Vaden faction sheet - " + playerName(g, q) + " owes them. Binding Debts: a neighbour may pay 1 trade good "
                        "at the start of the status phase to take back up to 2.";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- the Firmament / the Obsidian: plot cards with their puppets' control tokens
    if (te && (is("firmament") || is("obsidian"))) {
        const bool obs = is("obsidian"), open = obs || p == viewer;
        Group gr;
        gr.title = "Plots";
        gr.note = obs ? "Revealed plots: the players whose control tokens are on a plot are its puppets and suffer it."
                      : "Face-down plot cards, each with another player's control token. Only the Firmament know which plot is which.";
        gr.empty = "No plot is in play yet.";
        for (int k = 0; k < kPlotCount; ++k) {
            const bool in = (pl.plotsIn >> k) & 1;
            if (!in && p != viewer) continue;  // (the ones still in hand are the Firmament's secret)
            Piece pc;
            pc.kind = Kind::Plot;
            pc.value = k;
            pc.faction = f;
            pc.col = rgb(150, 90, 210);
            pc.hidden = in && !open;
            pc.dim = !in;
            std::string who;
            for (int q = 0; q < g.numPlayers; ++q)
                if ((pl.plotTok[size_t(k)] >> q) & 1) {
                    pc.tokens.push_back({factionOf(g, q), colourOf(g, q)});
                    who += (who.empty() ? "" : ", ") + playerName(g, q);
                }
            pc.label = pc.hidden ? std::string("?") : std::string(plotName(k));
            pc.name = pc.hidden ? std::string("A face-down plot") : std::string(plotName(k));
            if (!in) pc.name += " (in your hand)";
            pc.detail = (in ? (obs ? "Puppets: " : "Control token: ") + (who.empty() ? std::string("none") : who) + ". " : std::string("Not played yet. ")) +
                        (pc.hidden ? std::string("Which plot it is stays hidden until the Firmament become the Obsidian.") : std::string(plotText(k)));
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- the Vuil'raith: captured units, by owner and type
    if (is("cabal")) {
        Group gr;
        gr.title = "Captured units";
        gr.note = ability(f, "Devour");
        gr.empty = "Nothing captured yet.";
        std::vector<std::pair<int, int>> seen;  // (owner, type) in the order captured
        for (int16_t x : pl.captured) {
            const std::pair<int, int> k{x / 16, x % 16};
            auto it = std::find(seen.begin(), seen.end(), k);
            if (it == seen.end()) seen.push_back(k);
        }
        for (const auto& k : seen) {
            int n = 0;
            for (int16_t x : pl.captured) n += (x / 16 == k.first && x % 16 == k.second) ? 1 : 0;
            Piece pc;
            pc.kind = Kind::Unit;
            pc.value = k.second;
            pc.count = n;
            pc.col = colourOf(g, k.first);
            const bool token = k.first == kCapturedToken;
            pc.label = token ? std::string("token") : shortOf(g, k.first);
            pc.name = std::to_string(n) + " " + (token ? std::string() : playerName(g, k.first) + " ") + unitTypeName(UnitType(k.second), n > 1) +
                      (token ? std::string(" (tokens)") : std::string());
            pc.detail = token ? std::string("Fighters and infantry are captured as tokens; they don't belong to anyone now.")
                              : playerName(g, k.first) + " can't build these while you hold them. Amalgamation: give one back to produce a unit of "
                                                         "that type for free; deals can return them too.";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- Edyn: Sigils (systems with their mechs) - active on the board, the rest waiting in reinforcements
    if (ds && is("edyn")) {
        Group gr;
        gr.title = "Sigils";
        gr.note = "Rune Bearer: a system with an Edyn mech is a Sigil - an anomaly. Radiance puts other players' command tokens into them.";
        int onBoard = 0;
        for (size_t s = 0; s < g.systems.size(); ++s) {
            int m = g.systems[s].space[size_t(p)].count(UnitType::Mech);
            for (int16_t x : g.systems[s].planets) m += g.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech);
            if (!m) continue;
            onBoard += m;
            Piece pc;
            pc.kind = Kind::Sigil;
            pc.faction = f;
            pc.col = mine;
            pc.label = sysLabel(g, int(s));
            pc.name = "Active Sigil: " + sysLabel(g, int(s));
            pc.detail = std::to_string(m) + (m == 1 ? " mech holds it" : " mechs hold it") + ". Other players' ships can't move through it with "
                                                                                              "your ground forces there (Decree).";
            gr.pieces.push_back(pc);
        }
        for (int k = onBoard; k < unitLimitOf(g, p, UnitType::Mech); ++k) {
            Piece pc;
            pc.kind = Kind::Sigil;
            pc.faction = f;
            pc.col = mine;
            pc.dim = true;
            pc.label = "reserve";
            pc.name = "Inactive Sigil";
            pc.detail = "A Rune Bearer mech in your reinforcements - its Sigil lights up where it lands.";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- Myko-Mentori: the 4 Omen dice
    if (ds && is("mykomentori")) {
        Group gr;
        gr.title = "Omen dice";
        gr.note = ability(f, "Prescient Memories") + " Divination: " + ability(f, "Divination");
        for (int k = 0; k < 4; ++k) {
            Piece pc;
            pc.kind = Kind::Die;
            pc.faction = f;
            pc.value = pl.dsOmen[k];
            pc.dim = pl.dsOmen[k] <= 0;
            pc.col = mine;
            pc.label = pc.dim ? std::string("used") : std::to_string(pl.dsOmen[k]);
            pc.name = pc.dim ? std::string("Omen die - used") : "Omen die showing " + std::to_string(pl.dsOmen[k]);
            pc.detail = pc.dim ? "Rolled again at the start of the next strategy phase." : "Ready: spend it in place of one of your rolls.";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- Creuss: the alpha / beta / gamma wormhole tokens
    if (is("ghost")) {  // (the Creuss)
        Group gr;
        gr.title = "Wormhole tokens";
        gr.note = "The Creuss place their own alpha and beta wormhole tokens (agent, hero, flagship); for them every alpha and beta "
                  "wormhole is next to every other one (Quantum Entanglement).";
        static const char* kNames[3] = {"Alpha", "Beta", "Gamma"};
        for (int k = 0; k < 3; ++k) {
            const int s = g.creussTokens[size_t(k)];
            if (k == 2 && s < 0) continue;  // (the gamma one only when it is out)
            Piece pc;
            pc.kind = Kind::Wormhole;
            pc.value = k;
            pc.dim = s < 0;
            pc.label = s < 0 ? std::string("off board") : sysLabel(g, s);
            pc.name = std::string(kNames[k]) + " wormhole token";
            pc.detail = s < 0 ? std::string("Off the board.") : "In " + sysLabel(g, s) + ".";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- the Empyrean: Void Tethers (their Thunder's Edge breakthrough)
    if (is("empyrean")) {
        Group gr;
        gr.title = "Void Tethers";
        if (const BreakthroughDef* b = breakthroughsOn(g) ? breakthroughOf("empyrean") : nullptr) {
            gr.note = b->text;
            for (size_t k = 0; k < 2; ++k) {
                Piece pc;
                pc.kind = Kind::Tether;
                pc.faction = f;
                pc.col = mine;
                if (k < g.tethers.size()) {
                    const auto& t = g.tethers[k];
                    pc.label = sysLabel(g, t.first);
                    pc.name = "Tether: " + sysLabel(g, t.first) + " - " + sysLabel(g, t.second);
                    pc.detail = "Other players can't move across this border.";
                } else {
                    pc.dim = true;
                    pc.label = pl.breakthrough ? "ready" : "locked";
                    pc.name = "Void Tether - not placed";
                    pc.detail = pl.breakthrough ? "Placed when you activate a system with or next to your units or planets."
                                                : "Comes with the Empyrean breakthrough.";
                }
                gr.pieces.push_back(pc);
            }
        } else {
            gr.empty = "The Empyrean keep no tokens of their own in this game - their Void Tethers come with the Thunder's Edge "
                       "breakthroughs.";
        }
        out.push_back(gr);
    }

    // ---- the Naalu: the "0" token
    if (is("naalu")) {
        Group gr;
        gr.title = "The \"0\" token";
        gr.note = ability(f, "Telepathic");
        Piece pc;
        pc.kind = Kind::Zero;
        pc.faction = f;
        pc.col = theme::gold;
        const int h = g.naaluToken;
        pc.dim = h < 0;
        pc.label = h < 0 ? std::string("-") : h == p ? std::string("Naalu") : shortOf(g, h);
        pc.name = "Initiative 0";
        pc.detail = h < 0       ? std::string("Taken at the end of the strategy phase.")
                    : h == p    ? std::string("With the Naalu this round: they go first in the action phase.")
                                : "With " + playerName(g, h) + " this round (Gift of Prescience): they go first.";
        gr.pieces.push_back(pc);
        out.push_back(gr);
    }

    // ---- the Nekro: Valefar Assimilator X / Y (and Z, their breakthrough) on other factions' technologies
    if (is("nekro")) {
        Group gr;
        gr.title = "Assimilator tokens";
        gr.note = "Valefar Assimilator X and Y each copy a faction technology another player owns - the token sits on that card and "
                  "the Nekro have its text.";
        if (breakthroughIs(g, p, "nekro"))
            gr.note += " Z (their breakthrough) goes on a faction sheet instead: the Nekro flagship gains that faction's flagship abilities.";
        for (char L : {'X', 'Y'}) {
            const int t = L == 'X' ? pl.assimX : pl.assimY;
            const char* key = L == 'X' ? "vax" : "vay";
            const int own = findTech(key);
            if (own < 0 || !pl.hasTech(own)) {
                if (t < 0) continue;
            }
            Piece pc;
            pc.kind = Kind::Assim;
            pc.letter = L;
            pc.value = t;
            pc.dim = t < 0;
            pc.col = t >= 0 ? techColor(t) : theme::dim;
            pc.label = t >= 0 ? std::string(kTechs[size_t(t)].name) : std::string("free");
            pc.name = std::string("Valefar Assimilator ") + L + (t >= 0 ? std::string(": ") + kTechs[size_t(t)].name : std::string(" - not placed"));
            pc.detail = t >= 0 ? std::string(techSummary(t)) : std::string("Placed the next time you copy a faction technology.");
            gr.pieces.push_back(pc);
        }
        for (int8_t z : pl.nekroZ) {
            Piece pc;
            pc.kind = Kind::Assim;
            pc.letter = 'Z';
            pc.faction = z;
            pc.col = rgb(200, 60, 70);
            pc.label = factionShortName(z);
            pc.name = std::string("Valefar Assimilator Z: ") + factionShortName(z) + " flagship";
            pc.detail = "The Nekro flagship has this faction's flagship abilities.";
            gr.pieces.push_back(pc);
        }
        // (owner 2026-11-15: "the Valefar Z token is not showing") the breakthrough's 3 Z tokens from the moment it is
        // gained - the ones not placed yet dim, like an X / Y token waiting for a technology
        if (breakthroughIs(g, p, "nekro"))
            for (size_t k = pl.nekroZ.size(); k < 3; ++k) {
                Piece pc;
                pc.kind = Kind::Assim;
                pc.letter = 'Z';
                pc.dim = true;
                pc.col = theme::dim;
                pc.label = "free";
                pc.name = "Valefar Assimilator Z - not placed";
                pc.detail = "When you would gain another player's technology with a faction ability, you may place it on that player's "
                            "faction sheet instead: your flagship gains their flagship's abilities.";
                gr.pieces.push_back(pc);
            }
        gr.empty = "No technology assimilated yet (Valefar Assimilator X and Y are their faction technologies).";
        out.push_back(gr);
    }

    // ---- (owner 2026-11-15) a Nekro "Z" assimilator token on THIS faction sheet (Valefar Assimilator Z, Thunder's Edge): the
    // card puts it "on that player's faction sheet"
    if (!is("nekro"))
        for (int q = 0; q < g.numPlayers; ++q) {
            if (q == p) continue;
            const auto& zs = g.players[size_t(q)].nekroZ;
            if (std::find(zs.begin(), zs.end(), int8_t(f)) == zs.end()) continue;
            Group gr;
            gr.title = "Assimilator token";
            gr.note = "The Nekro placed a Valefar Assimilator Z token on this faction sheet: their flagship has this faction's flagship abilities too.";
            Piece pc;
            pc.kind = Kind::Assim;
            pc.letter = 'Z';
            pc.faction = g.players[size_t(q)].faction;
            pc.col = rgb(200, 60, 70);
            pc.label = shortOf(g, q);
            pc.name = std::string("Valefar Assimilator Z (") + playerName(g, q) + ")";
            pc.detail = playerName(g, q) + "'s flagship has the text abilities of this faction's flagship in addition to its own.";
            gr.pieces.push_back(pc);
            out.push_back(gr);
        }

    // ---- the Titans: sleeper tokens (5)
    if (is("titans")) {
        Group gr;
        gr.title = "Sleeper tokens";
        gr.note = ability(f, "Awaken");
        int n = 0;
        for (size_t x = 0; x < g.planets.size(); ++x)
            if (g.planets[x].sleeper) {
                ++n;
                Piece pc;
                pc.kind = Kind::Sleeper;
                pc.faction = f;
                pc.col = mine;
                pc.label = planetLabel(g, int(x));
                pc.name = "Sleeper on " + planetLabel(g, int(x));
                pc.detail = "Activate its system to wake it: it becomes a PDS (Awaken).";
                gr.pieces.push_back(pc);
            }
        for (int k = n; k < 5; ++k) {
            Piece pc;
            pc.kind = Kind::Sleeper;
            pc.faction = f;
            pc.col = mine;
            pc.dim = true;
            pc.label = "left";
            pc.name = "Sleeper token - in supply";
            pc.detail = "Not on a planet yet (Terragenesis places them after exploring).";
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- Last Bastion: galvanize tokens on their units
    if (te && is("bastion")) {
        Group gr;
        gr.title = "Galvanize tokens";
        gr.note = ability(f, "Galvanize");
        gr.empty = "No unit is galvanized right now.";
        for (size_t s = 0; s < g.systems.size(); ++s) {
            unitsLine(g.systems[s].space[size_t(p)], int(s), g, gr, "In space");
            for (int16_t x : g.systems[s].planets) unitsLine(g.planets[size_t(x)].units[size_t(p)], int(s), g, gr, ("On " + planetLabel(g, x)).c_str());
        }
        out.push_back(gr);
    }

    // ---- the Crimson Rebellion: breaches, active and inactive
    if (te && is("crimson")) {
        Group gr;
        gr.title = "Breaches";
        gr.note = ability(f, "Incursion");
        gr.empty = "No breach on the board.";
        for (int pass = 2; pass >= 1; --pass)
            for (size_t s = 0; s < g.systems.size(); ++s) {
                if (g.systems[s].breach != pass) continue;
                Piece pc;
                pc.kind = Kind::Breach;
                pc.value = pass;
                pc.dim = pass == 1;
                pc.col = pass == 2 ? rgb(255, 90, 180) : rgb(130, 120, 135);
                pc.label = sysLabel(g, int(s));
                pc.name = std::string(pass == 2 ? "Active" : "Inactive") + " breach: " + sysLabel(g, int(s));
                pc.detail = pass == 2 ? "Active breaches are next to each other." : "Flip it by activating its system.";
                gr.pieces.push_back(pc);
            }
        out.push_back(gr);
    }

    // ---- Kjalengard: Glory tokens (3)
    if (ds && is("kjalengard")) {
        Group gr;
        gr.title = "Glory tokens";
        gr.note = ability(f, "Glory");
        for (size_t k = 0; k < 3; ++k) {
            Piece pc;
            pc.kind = Kind::Glory;
            pc.faction = f;
            pc.col = theme::gold;
            if (k < g.dsGlory.size()) {
                pc.label = sysLabel(g, g.dsGlory[k]);
                pc.name = "Glory: " + sysLabel(g, g.dsGlory[k]);
                pc.detail = "Valor: in this system each natural 10 on a combat roll makes 1 more hit.";
            } else {
                pc.dim = true;
                pc.label = "left";
                pc.name = "Glory token - not placed";
                pc.detail = "Placed after you win a combat.";
            }
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }

    // ---- Veldyr: the 4 Branch Offices
    if (ds && is("veldyr")) {
        Group gr;
        gr.title = "Branch Offices";
        gr.note = ability(f, "Holding Company");
        for (int k = 0; k < 4; ++k) {
            const int at = g.dsBranch[k];
            if (k == 0 && at < 0 && g.fixes < 6) continue;  // (games started before 0.44.1 never had Tax Haven)
            const int16_t note = makeNote(p, kNoteFaction + k);
            Piece pc;
            pc.kind = Kind::Branch;
            pc.faction = f;
            pc.col = mine;
            pc.name = noteNameOf(g, note);
            if (at >= 0) {
                const int owner = g.planets[size_t(at)].owner;
                pc.label = planetLabel(g, at);
                pc.detail = "Open on " + planetLabel(g, at) + (owner >= 0 ? " (" + playerName(g, owner) + ")" : std::string()) + ".";
            } else {
                int holder = -1;
                for (int q = 0; q < g.numPlayers && holder < 0; ++q) {
                    const auto& v = g.players[size_t(q)].notes;
                    if (std::find(v.begin(), v.end(), note) != v.end()) holder = q;
                }
                pc.dim = true;
                pc.label = "to place";
                pc.detail = holder == p || holder < 0 ? std::string("Not placed - still with the Veldyr. It opens on a planet of the player who receives it.")
                                                       : "Not placed yet - held by " + playerName(g, holder) + ".";
            }
            gr.pieces.push_back(pc);
        }
        out.push_back(gr);
    }
    return out;
}

// ======================================================================== drawing
void drawPiece(Canvas& c, const Piece& pc, float cx, float cy, float s, uint32_t now, int alpha) {
    const int a = pc.dim ? alpha * 2 / 5 : alpha;
    const float r = s * 0.5f;
    switch (pc.kind) {
        case Kind::Control:
            cards::controlToken(c, pc.faction, pc.col, int(cx), int(cy), int(s), a);
            break;
        case Kind::Plot: {  // a card on its long side, the puppets' control tokens along its foot
            const float w = s * 0.74f, h = s;
            const int x = int(cx - w / 2), y = int(cy - h / 2);
            c.fillRoundRect(x, y, int(w), int(h), std::max(2, int(s / 9)), pc.hidden ? rgb(40, 20, 58) : rgb(58, 30, 84), a);
            c.frameRoundRect(x, y, int(w), int(h), std::max(2, int(s / 9)), pc.dim ? rgb(110, 90, 130) : rgb(190, 140, 240), 1, a);
            if (pc.hidden || s < 34) {  // the back: the faction's emblem
                if (pc.faction >= 0) drawFactionIcon(c, pc.faction, int(cx - w * 0.32f), int(cy - h * 0.3f), int(w * 0.64f), a * 3 / 4);
            } else {
                for (int k = 0; k < 3; ++k)  // a few lines of "text"
                    c.fillRect(x + int(w * 0.16f), y + int(h * (0.22f + 0.12f * float(k))), int(w * (k == 2 ? 0.45f : 0.68f)), std::max(1, int(s / 30)),
                               rgb(200, 170, 240), a / 2);
            }
            const int n = int(pc.tokens.size());
            for (int k = 0; k < n; ++k) {
                const float d = s * 0.38f, tx = cx + (float(k) - float(n - 1) / 2.f) * d * 0.8f;
                cards::controlToken(c, pc.tokens[size_t(k)].first, pc.tokens[size_t(k)].second, int(tx), int(cy + h * 0.32f), int(d), alpha);
            }
            break;
        }
        case Kind::Unit: {
            static const char* kArt[] = {"unit/carrier", "unit/cruiser", "unit/destroyer", "unit/dreadnought", "unit/flagship", "unit/warsun",
                                         "unit/fighter", "unit/infantry", "unit/mech", "unit/pds", "unit/spacedock"};
            const char* art = "unit/fighter";
            switch (UnitType(pc.value)) {
                case UnitType::Carrier: art = kArt[0]; break;
                case UnitType::Cruiser: art = kArt[1]; break;
                case UnitType::Destroyer: art = kArt[2]; break;
                case UnitType::Dreadnought: art = kArt[3]; break;
                case UnitType::Flagship:
                case UnitType::Flagship2: art = kArt[4]; break;
                case UnitType::WarSun: art = kArt[5]; break;
                case UnitType::Infantry: art = kArt[7]; break;
                case UnitType::Mech: art = kArt[8]; break;
                case UnitType::PDS: art = kArt[9]; break;
                case UnitType::SpaceDock: art = kArt[10]; break;
                default: break;
            }
            c.fillCircle(cx, cy, r, rgb(30, 14, 22), a * 3 / 4);  // the Vuil'raith's maw behind it
            c.ring(cx, cy, r, std::max(1.f, s / 18.f), rgb(170, 40, 60), a);
            if (!icons::drawUnitArt(c, art, cx, cy, int(s * 0.86f), int(s * 0.6f), pc.col, false, a))
                drawTextAligned(c, FONT_SMB, int(cx - r), int(cy - 7), int(s), Align::Center, unitTypeShort(UnitType(pc.value)), pc.col);
            break;
        }
        case Kind::Die:
            dice::drawD10(c, cx, cy, s * 0.92f, pc.value > 0 ? pc.value : 10, false, -1.f, pc.faction, alpha);
            if (pc.dim) c.fillCircle(cx, cy, s * 0.46f, rgb(235, 90, 40), alpha * 150 / 255);
            break;
        case Kind::Wormhole: {
            static const uint32_t kCol[3] = {rgb(240, 150, 60), rgb(90, 200, 110), rgb(230, 90, 200)};
            static const char* kSym[3] = {"\xCE\xB1", "\xCE\xB2", "\xCE\xB3"};
            const uint32_t wc = kCol[std::max(0, std::min(2, pc.value))];
            c.fillCircle(cx, cy, r, rgb(10, 10, 20), a);
            const float spin = float(now % 4000u) / 4000.f * 2.f * kPi;
            for (int k = 0; k < 3; ++k)  // a slow swirl
                for (int i = 0; i < 10; ++i) {
                    const float u = float(i) / 9.f, ang = spin + float(k) * 2.f * kPi / 3.f + u * 2.6f;
                    c.fillCircle(cx + std::cos(ang) * r * (0.2f + 0.65f * u), cy + std::sin(ang) * r * (0.2f + 0.65f * u), std::max(0.8f, s * 0.04f),
                                 wc, a * (pc.dim ? 1 : 2) / 3);
                }
            c.ring(cx, cy, r, std::max(1.f, s / 14.f), wc, a);
            drawTextAligned(c, s >= 34 ? FONT_LG : FONT_SMB, int(cx - r), int(cy - (s >= 34 ? 10 : 7)), int(s), Align::Center, kSym[std::max(0, std::min(2, pc.value))],
                            pc.dim ? theme::dim : theme::text);
            break;
        }
        case Kind::Zero: {
            c.fillCircle(cx, cy, r, rgb(40, 30, 8), a);
            c.fillCircle(cx, cy, r * 0.86f, rgb(222, 180, 70), a);
            c.ring(cx, cy, r * 0.86f, std::max(1.f, s / 16.f), rgb(255, 236, 170), a);
            const FontData& zf = s >= 34 ? FONT_XL : FONT_MDB;
            drawTextAligned(c, zf, int(cx - r), int(cy) - zf.lineHeight / 2, int(s), Align::Center, "0", rgb(40, 28, 6));
            break;
        }
        case Kind::Assim: {  // a small technology card with the red token on it
            const float w = s * 0.74f, h = s;
            const int x = int(cx - w / 2), y = int(cy - h / 2);
            c.fillRoundRect(x, y, int(w), int(h), std::max(2, int(s / 9)), rgb(16, 22, 38), a);
            c.fillRect(x + 1, y + 1, int(w) - 2, std::max(2, int(h * 0.16f)), pc.letter == 'Z' ? rgb(90, 40, 50) : pc.col, a);
            c.frameRoundRect(x, y, int(w), int(h), std::max(2, int(s / 9)), pc.col, 1, a);
            if (pc.letter == 'Z' && pc.faction >= 0) drawFactionIcon(c, pc.faction, int(cx - w * 0.3f), int(cy - h * 0.18f), int(w * 0.6f), a);
            const float tr = s * 0.22f, tx = cx + w * 0.22f, ty = cy + h * 0.26f;
            c.fillCircle(tx, ty, tr + 1.5f, rgb(20, 4, 6), alpha);
            c.fillCircle(tx, ty, tr, pc.dim ? rgb(90, 40, 44) : rgb(196, 30, 40), alpha);
            c.ring(tx, ty, tr, 1.2f, rgb(255, 170, 160), alpha * 9 / 10);
            drawTextAligned(c, s >= 34 ? FONT_SMB : FONT_XS, int(tx - tr * 2), int(ty - (s >= 34 ? 7 : 5)), int(tr * 4), Align::Center,
                            std::string(1, pc.letter), theme::text);
            break;
        }
        case Kind::Sleeper: {  // a dark disc with a teal ring and a closed eye
            c.fillCircle(cx, cy, r, rgb(16, 34, 38), a);
            c.ring(cx, cy, r, std::max(1.2f, s / 12.f), rgb(90, 220, 200), a);
            for (int i = 0; i < 7; ++i) {  // the eyelid
                const float u = float(i) / 6.f, ang = kPi * (0.15f + 0.7f * u);
                c.fillCircle(cx + std::cos(ang) * r * 0.5f, cy - r * 0.15f + std::sin(ang) * r * 0.32f, std::max(0.8f, s * 0.035f), rgb(140, 240, 225), a);
            }
            for (int k = -1; k <= 1; ++k)  // lashes
                c.line(cx + float(k) * r * 0.24f, cy + r * 0.2f, cx + float(k) * r * 0.32f, cy + r * 0.38f, rgb(140, 240, 225), std::max(1.f, s / 24.f), a);
            break;
        }
        case Kind::Galvanize: {  // a yellow lightning bolt on a dark disc
            c.fillCircle(cx, cy, r, rgb(30, 26, 10), a);
            c.ring(cx, cy, r, std::max(1.f, s / 16.f), rgb(255, 210, 80), a);
            float xs[6] = {cx + r * 0.12f, cx - r * 0.42f, cx - r * 0.02f, cx - r * 0.16f, cx + r * 0.42f, cx + r * 0.02f};
            float ys[6] = {cy - r * 0.72f, cy + r * 0.1f, cy + r * 0.1f, cy + r * 0.72f, cy - r * 0.12f, cy - r * 0.12f};
            c.fillPolygon(xs, ys, 6, rgb(255, 220, 90), a);
            break;
        }
        case Kind::Breach: {
            const bool active = pc.value == 2;
            const float pulse = active ? 0.6f + 0.4f * std::sin(float(now % 1600) / 1600.f * 2.f * kPi) : 0.f;
            if (active) c.fillCircle(cx, cy, r * 1.15f, rgb(200, 30, 120), int(float(a) * (0.25f + 0.2f * pulse)));
            c.fillCircle(cx, cy, r * 0.9f, rgb(16, 6, 14), a);
            const uint32_t col = active ? mix(rgb(255, 80, 170), rgb(255, 220, 240), int(pulse * 255)) : rgb(120, 110, 125);
            const float xs[5] = {cx - r * 0.6f, cx - r * 0.18f, cx + r * 0.05f, cx + r * 0.26f, cx + r * 0.6f};
            const float ys[5] = {cy + r * 0.3f, cy - r * 0.08f, cy + r * 0.18f, cy - r * 0.3f, cy - r * 0.04f};
            for (int i = 0; i < 4; ++i) c.line(xs[i], ys[i], xs[i + 1], ys[i + 1], col, std::max(1.5f, s / 14.f), a);
            c.ring(cx, cy, r * 0.9f, std::max(1.f, s / 24.f), active ? rgb(255, 120, 200) : rgb(90, 85, 95), a);
            break;
        }
        case Kind::Glory: {  // a gold laurel star
            c.fillCircle(cx, cy, r, rgb(40, 30, 10), a);
            c.ring(cx, cy, r, std::max(1.f, s / 16.f), theme::gold, a);
            float xs[10], ys[10];
            for (int k = 0; k < 10; ++k) {
                const float ang = -kPi / 2.f + float(k) * kPi / 5.f, rr = (k % 2 ? 0.3f : 0.7f) * r;
                xs[k] = cx + std::cos(ang) * rr;
                ys[k] = cy + std::sin(ang) * rr;
            }
            c.fillPolygon(xs, ys, 10, rgb(250, 210, 110), a);
            break;
        }
        case Kind::Branch: {  // a tower block in the Veldyr's colour
            c.fillRoundRect(int(cx - r), int(cy - r), int(s), int(s), std::max(2, int(s / 8)), rgb(18, 24, 40), a);
            c.frameRoundRect(int(cx - r), int(cy - r), int(s), int(s), std::max(2, int(s / 8)), pc.col, 1, a);
            const float bw = s * 0.22f;
            for (int k = 0; k < 3; ++k) {
                const float hh = s * (k == 1 ? 0.62f : 0.42f), bx = cx + (float(k) - 1.f) * bw * 1.05f - bw / 2.f;
                c.fillRect(int(bx), int(cy + r * 0.72f - hh), int(bw), int(hh), shade(pc.col, k == 1 ? 1.1f : 0.8f), a);
                for (int w = 0; w < 3; ++w)
                    c.fillRect(int(bx + bw * 0.3f), int(cy + r * 0.72f - hh + hh * (0.15f + 0.27f * float(w))), std::max(1, int(bw * 0.4f)),
                               std::max(1, int(s / 26)), pc.dim ? rgb(60, 60, 70) : rgb(255, 236, 160), a);
            }
            break;
        }
        case Kind::Sigil: {  // a glowing rune
            if (!pc.dim) c.fillCircle(cx, cy, r * 1.1f, pc.col, a / 5);
            c.fillCircle(cx, cy, r * 0.9f, rgb(14, 16, 30), a);
            c.ring(cx, cy, r * 0.9f, std::max(1.f, s / 16.f), pc.col, a);
            const uint32_t rc = pc.dim ? rgb(110, 110, 130) : mix(pc.col, rgb(255, 255, 255), 120);
            const float w = std::max(1.2f, s / 14.f);
            c.line(cx, cy - r * 0.6f, cx, cy + r * 0.6f, rc, w, a);
            c.line(cx - r * 0.45f, cy - r * 0.2f, cx, cy - r * 0.6f, rc, w, a);
            c.line(cx + r * 0.45f, cy - r * 0.2f, cx, cy - r * 0.6f, rc, w, a);
            c.line(cx - r * 0.4f, cy + r * 0.25f, cx + r * 0.4f, cy + r * 0.25f, rc, w, a);
            break;
        }
        case Kind::Tether: {  // two linked rings
            const uint32_t tc = pc.dim ? rgb(110, 110, 130) : rgb(170, 120, 255);
            c.ring(cx - r * 0.3f, cy, r * 0.5f, std::max(1.2f, s / 12.f), tc, a);
            c.ring(cx + r * 0.3f, cy, r * 0.5f, std::max(1.2f, s / 12.f), tc, a);
            break;
        }
    }
    if (pc.count > 1) {  // xN at the lower right
        const std::string t = "x" + std::to_string(pc.count);
        const int tw = textWidth(FONT_XS, t) + 4;
        c.fillRoundRect(int(cx + r * 0.55f), int(cy + r * 0.4f), tw, 11, 3, rgb(8, 10, 18), 220);
        drawText(c, FONT_XS, int(cx + r * 0.55f) + 2, int(cy + r * 0.4f), t, theme::text);
    }
}

void drawTray(Canvas& c, const std::vector<Group>& gs, Rect r, uint32_t now, bool hover) {
    if (gs.empty()) return;
    c.fillRoundRect(r.x, r.y, r.w, r.h, 8, hover ? rgb(26, 34, 58) : rgb(18, 24, 42), 235);
    c.frameRoundRect(r.x, r.y, r.w, r.h, 8, hover ? theme::gold : theme::border, 1);
    std::string title;
    for (const Group& g : gs) title += (title.empty() ? "" : " \xC2\xB7 ") + g.title;
    drawText(c, FONT_SMB, r.x + 8, r.y + 4, fitText(FONT_SMB, title, r.w - 90), theme::gold);
    drawTextAligned(c, FONT_XS, r.x, r.y + 6, r.w - 8, Align::Right, hover ? "click to open" : keyLabel("Y") + ": open", theme::faint);
    // every piece in one row, as large as fits
    std::vector<const Piece*> all;
    for (const Group& g : gs)
        for (const Piece& p : g.pieces) all.push_back(&p);
    const int top = r.y + 22, room = r.h - 22 - (r.h >= 58 ? 12 : 2);
    if (all.empty()) {
        drawText(c, FONT_XS, r.x + 10, top + 4, fitText(FONT_XS, gs[0].empty.empty() ? "None yet." : gs[0].empty, r.w - 20), theme::dim);
        return;
    }
    const int n = int(all.size());
    const float slot = std::min(float(room) + 10.f, float(r.w - 12) / float(n));
    const float size = std::min(float(room), slot - 4.f);
    const float x0 = float(r.x + 6) + slot / 2.f;
    for (int i = 0; i < n; ++i) {
        const float cx = x0 + slot * float(i), cy = float(top) + float(room) / 2.f;
        drawPiece(c, *all[size_t(i)], cx, cy, size, now);
        if (r.h >= 58 && slot >= 26)
            drawTextAligned(c, FONT_XS, int(cx - slot / 2), r.y + r.h - 12, int(slot), Align::Center, fitText(FONT_XS, all[size_t(i)]->label, int(slot) - 2),
                            all[size_t(i)]->dim ? theme::faint : theme::dim);
    }
}

int drawDetail(Canvas& c, const Game& g, int p, const std::vector<Group>& gs, Rect r, uint32_t now, int scroll) {
    (void)g;
    (void)p;
    const Rect area{r.x + 10, r.y + 44, r.w - 20, r.h - 52};
    c.setClip(area);
    int y = area.y - scroll;
    const int colW = (area.w - 10) / 2;
    for (const Group& gr : gs) {
        drawText(c, FONT_MDB, area.x + 4, y, gr.title, theme::gold);
        y += 20;
        if (!gr.note.empty()) {
            for (const std::string& l : wrapText(FONT_XS, gr.note, area.w - 8)) {
                drawText(c, FONT_XS, area.x + 4, y, l, theme::dim);
                y += FONT_XS.lineHeight + 1;
            }
            y += 4;
        }
        if (gr.pieces.empty()) {
            drawText(c, FONT_SM, area.x + 8, y, fitText(FONT_SM, gr.empty.empty() ? "None yet." : gr.empty, area.w - 16), theme::faint);
            y += 22;
        }
        const int tw = colW - 66;
        for (size_t i = 0; i < gr.pieces.size(); i += 2) {
            // a row of two: as tall as the longer text needs (the Nekro's technologies are read here)
            int rowH = 64;
            for (size_t k = i; k < i + 2 && k < gr.pieces.size(); ++k)
                rowH = std::max(rowH, 29 + int(wrapText(FONT_XS, gr.pieces[k].detail, tw).size()) * (FONT_XS.lineHeight + 1));
            for (size_t k = i; k < i + 2 && k < gr.pieces.size(); ++k) {
                const Piece& pc = gr.pieces[k];
                const int x = area.x + int(k - i) * (colW + 10);
                c.fillRoundRect(x, y, colW, rowH - 4, 8, rgb(18, 24, 42), 230);
                drawPiece(c, pc, float(x + 30), float(y + std::min(rowH - 4, 64) / 2), 44.f, now);
                const int tx = x + 60;
                drawText(c, FONT_SMB, tx, y + 4, fitText(FONT_SMB, pc.name, tw), pc.dim ? theme::dim : theme::text);
                int ly = y + 21;
                for (const std::string& l : wrapText(FONT_XS, pc.detail, tw)) {
                    drawText(c, FONT_XS, tx, ly, l, theme::dim);
                    ly += FONT_XS.lineHeight + 1;
                }
            }
            y += rowH;
        }
        y += 8;
    }
    c.resetClip();
    return y + scroll - area.y;
}

}  // namespace pieces
