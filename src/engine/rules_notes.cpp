// Promissory notes (LRR 69): the four generic notes every player owns.
// Without negotiations they change hands only through game effects such as
// Diplomatic Pressure; trading arrives with the Negotiations menu.
#include <algorithm>
#include <cstring>

#include "engine/rules.h"

namespace ti {

void Rules::giveNote(int from, int to, int note) {
    auto& h = G.players[from].notes;
    auto it = std::find(h.begin(), h.end(), int16_t(note));
    if (it == h.end()) return;
    h.erase(it);
    say(from, pn(from) + " gives " + pn(to) + " a promissory note (" + noteNameOf(G, note) + ").");
    receiveNote(to, note);
}

void Rules::receiveNote(int p, int note) {
    const int owner = noteOwner(note), type = noteType(note);
    if (owner == p) {  // a note returned to its owner goes back to their hand
        G.players[p].notes.push_back(int16_t(note));
        return;
    }
    if (type == kNoteAlliance) {
        // Alliance goes face up into the play area: its holder can use the owner's commander.
        G.players[p].notesInPlay.push_back(int16_t(note));
        say(p, pn(p) + " now has " + pn(owner) + "'s Alliance.");
        return;
    }
    if (type == kNoteSupportThrone) {
        // Support for the Throne goes face up into the play area: +1 victory point.
        G.players[p].notesInPlay.push_back(int16_t(note));
        gainVP(p, 1, std::string("Support for the Throne from ") + pn(owner));
        return;
    }
    // 0.32.0 (owner, new games): Trade Convoys goes to the hand and is played as an ACTION (Rules::factionAction2)
    // 0.32.1 (owner, new games): Dark Pact too
    if (type == kNoteFaction && ((isFaction(G, owner, "empyrean") && !(G.extras & 64)) || (isFaction(G, owner, "hacan") && !(G.extras & 32)))) {
        // Dark Pact / Trade Convoys work from the play area: placed face up at once
        G.players[p].notesInPlay.push_back(int16_t(note));
        say(p, pn(p) + " places " + pn(owner) + "'s " + noteNameOf(G, note) + " face up.");
        if (isFaction(G, owner, "hacan"))  // 0.30.0 (owner couldn't find how to play it): it works from the play area
            feedNote(p, "Trade Convoys is in play for " + pn(p) + ": deals with players who aren't neighbours are open now.");
        return;
    }
    if (type == kNoteFaction && isFaction(G, owner, "celdauri")) {  // Trade Alliance (Discordant Stars): face up, like an Alliance
        G.players[p].notesInPlay.push_back(int16_t(note));
        say(p, pn(p) + " places " + pn(owner) + "'s Trade Alliance face up.");
        return;
    }
    if (type == kNoteFaction && isFaction(G, owner, "augers")) {  // Read the Fates (Discordant Stars): face up
        G.players[p].notesInPlay.push_back(int16_t(note));
        say(p, pn(p) + " places " + pn(owner) + "'s Read the Fates face up.");
        const int16_t o = G.players[size_t(owner)].dsSheet;
        if (o >= 0 && G.players[size_t(p)].human)
            feedNote(p, std::string("Read the Fates: the next public objective will be ") + kObjectives[o].name + " - " + objectiveSummary(o));
        return;
    }
    if ((G.modules & kModDiscordant) && type >= kNoteFaction && type <= kNoteFaction4 && isFaction(G, owner, "veldyr")) {
        dsBranchReceived(p, note);  // Corporate Entity (Discordant Stars): a Branch Office attaches at once
        return;
    }
    if (type == kNoteFaction && isFaction(G, owner, "firmament")) {  // Black Ops: resolved at once, then purged
        blackOps(p, note);
        return;
    }
    G.players[p].notes.push_back(int16_t(note));
    if (type == kNoteFaction && isFaction(G, owner, "hacan") && G.players[p].human)  // 0.32.0: where to play it
        feedNote(p, "Trade Convoys is in your hand: play it from Component Actions (it uses your action) to trade "
                    "with players who aren't neighbours.");
    if (type == kNoteFaction && isFaction(G, owner, "empyrean") && G.players[p].human)  // 0.32.1
        feedNote(p, "Dark Pact is in your hand: play it from Component Actions (it uses your action). Then giving the "
                    "Empyrean as many commodities as your commodity value earns you each 1 trade good.");
}

void Rules::returnNote(int holder, int note) {
    Player& pl = G.players[holder];
    for (auto* v : {&pl.notes, &pl.notesInPlay}) {
        auto it = std::find(v->begin(), v->end(), int16_t(note));
        if (it != v->end()) {
            v->erase(it);
            break;
        }
    }
    int owner = noteOwner(note);
    if (owner >= 0 && owner < G.numPlayers && !G.players[owner].eliminated) G.players[owner].notes.push_back(int16_t(note));
    // High Security Vault (Vaden breakthrough, 0.43.0): a note of theirs given back - 1 of that player's control tokens on
    // their sheet
    if ((G.modules & kModDiscordant) && breakthroughsOn(G) && owner >= 0 && owner < G.numPlayers && owner != holder &&
        breakthroughIs(G, owner, "vaden") && G.dsDebt[holder] < 6) {
        ++G.dsDebt[holder];
        say(owner, pn(holder) + " puts 1 control token on " + pn(owner) + "'s sheet (High Security Vault).");
    }
    // Renegotiation (Uncharted Space): a faction note used by another player
    const int type = noteType(note);
    if ((G.modules & kModUncharted) && !quietNoteReturn_ && (type == kNoteFaction || type == kNoteFaction2) && owner >= 0 &&
        owner != holder && !G.players[owner].eliminated && !G.players[holder].notes.empty()) {
        WinCtx w;
        w.w = Win::NoteResolved;
        w.a = owner;
        w.b = holder;
        window(w);
    }
}

// Offers the holder the chance to play one of 'owner's notes of 'type' now.
bool Rules::playNoteWindow(int holder, int type, int owner, const std::string& prompt) {
    int note = makeNote(owner, type);
    const auto& h = G.players[holder].notes;
    if (std::find(h.begin(), h.end(), int16_t(note)) == h.end()) return false;
    int c = pick(holder, Tag::PlayNote, prompt, {{note, noteNameOf(G, note), noteTextOf(G, note)}, {-1, "Not now", ""}});
    if (c < 0) return false;
    say(holder, pn(holder) + " plays " + pn(owner) + "'s " + noteNameOf(G, note) + ".");
    returnNote(holder, note);
    return true;
}

// Support for the Throne: lost when you activate a system with its owner's units.
void Rules::supportForTheThroneCheck(int p, int sys) {
    Player& pl = G.players[p];
    for (int16_t note : std::vector<int16_t>(pl.notesInPlay)) {
        int owner = noteOwner(note);
        // Promise of Protection (Mentak), Antivirus (Nekro), Dark Pact / Blood Pact (Empyrean) and Trade Convoys (Hacan) go back the same way
        if (((noteType(note) == kNoteFaction && (isFaction(G, owner, "mentak") || isFaction(G, owner, "nekro") || isFaction(G, owner, "empyrean") ||
                                                 isFaction(G, owner, "hacan"))) ||
             (noteType(note) == kNoteFaction2 && isFaction(G, owner, "empyrean"))) &&
            hasUnits(G, owner, sys)) {
            quietNoteReturn_ = true;  // (sent back, not used)
            returnNote(p, note);
            quietNoteReturn_ = false;
            say(p, pn(p) + "'s " + noteNameOf(G, note) + " goes back to " + pn(owner) + ".");
            continue;
        }
        if (noteType(note) != kNoteSupportThrone) continue;
        if (!hasUnits(G, owner, sys)) continue;
        returnNote(p, note);
        loseVP(p, 1, std::string("activated a system with ") + pn(owner) + "'s units - Support for the Throne goes back");
    }
}

// Trade Agreement: when its owner refills commodities, they go to the holder.
void Rules::tradeAgreement(int owner) {
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == owner || G.players[q].eliminated) continue;
        int note = makeNote(owner, kNoteTradeAgreement);
        const auto& h = G.players[q].notes;
        if (std::find(h.begin(), h.end(), int16_t(note)) == h.end()) continue;
        if (!playNoteWindow(q, kNoteTradeAgreement, owner, "Play " + pn(owner) + "'s Trade Agreement?")) continue;
        if (G.modules & kModUncharted) {  // Escape Clause (Uncharted Space): the owner keeps the commodities
            WinCtx w;
            w.w = Win::TradeAgreement;
            w.a = owner;
            w.b = q;
            w.value = G.players[owner].commodities;
            if (cardHas(window(w), "escape_clause")) {
                say(owner, pn(owner) + " keeps the commodities; " + pn(q) + " gains 2 trade goods instead (Escape Clause).");
                gainTG(q, 2);
                return;
            }
        }
        int n = G.players[owner].commodities;
        G.players[owner].commodities = 0;
        gainTG(q, n);
        say(q, pn(q) + " receives " + std::to_string(n) + " commodities from " + pn(owner) + " (as trade goods).");
        // (fixes 26, the Empyrean review - owner 2026-11-12) Dark Pact: "When you give a number of commodities to the
        // Empyrean player equal to your maximum commodity value, you each gain 1 trade good" - this way too
        if (G.fixes >= 26 && n > 0 && isFaction(G, q, "empyrean") && n >= commodityValue(G, owner))
            for (int16_t x : G.players[owner].notesInPlay)
                if (noteOwner(x) == q && noteType(x) == kNoteFaction) {
                    gainTG(owner, 1);
                    gainTG(q, 1);
                    say(owner, pn(owner) + " and " + pn(q) + " each gain 1 trade good (Dark Pact).");
                    break;
                }
        return;
    }
}

// Ceasefire: after its owner activates a system with your units, they can't move in.
bool Rules::ceasefire(int active, int sys) {
    for (int q : windowOrder()) {
        if (q == active || !hasUnits(G, q, sys)) continue;
        if (playNoteWindow(q, kNoteCeasefire, active, "Play " + pn(active) + "'s Ceasefire? They can't move units into " + sysName(sys) + "."))
            return true;
    }
    return false;
}

}  // namespace ti
