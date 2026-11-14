// The eight strategy cards: primary abilities for the card's owner and
// secondary abilities for everyone else.
#include <algorithm>
#include <cstring>

#include "engine/deal.h"
#include "engine/rules.h"

namespace ti {

void Rules::strategicAction(int p, int card) {
    Player& pl = G.players[p];
    pl.cardsUsed = uint8_t(pl.cardsUsed | (1u << card));
    const int prev = strategicPlayer_;
    strategicPlayer_ = p;
    primaryAbility(p, card);
    if (G.modules & kModDiscordant) dsAfterPrimary(p, card);  // Grace (Edyn)
    planesplitter(p);  // Planesplitter (Obsidian): an ingress token moves
    for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
        if (q == p) continue;
        secondaryAbility(q, card);
    }
    strategicPlayer_ = prev;
}

void Rules::primaryAbility(int p, int card) {
    // Brikk Strain (Kyro hero, 0.43.0 D17): on the card it is attached to, other players resolve 1 clause of the primary
    struct BrikkGuard {
        int& c;
        ~BrikkGuard() { c = -1; }
    } brikk{brikkClause_};
    if (G.fixes >= 2 && (G.modules & kModDiscordant) && G.blight == card && G.blightOwner >= 0 && !fac(p, "kyro")) {
        const bool te = (G.modules & kModThundersEdge) != 0;
        std::vector<Option> opts;
        switch (card) {
            case 0: opts = {{0, "Gain 3 command tokens", ""}, {1, "Buy command tokens with influence", ""}}; break;
            case 1: opts = {{0, "Protect a system", "Every other player places a command token there."}, {1, "Ready up to 2 planets", ""}}; break;
            case 2: opts = {{0, "Choose the new speaker", ""}, {1, "Draw 2 action cards", ""}, {2, "Look at the top 2 agendas", ""}}; break;
            case 3:
                opts = te ? std::vector<Option>{{0, "A structure, or production at a space dock", ""}, {1, "Then 1 structure", ""}}
                          : std::vector<Option>{{0, "1 PDS or space dock", ""}, {1, "1 more PDS", ""}};
                break;
            case 4: opts = {{0, "Gain 3 trade goods", ""}, {1, "Refill your commodities", ""}, {2, "Let other players refill", ""}}; break;
            case 5:
                opts = te ? std::vector<Option>{{0, "A tactical action without a command token", ""}, {1, "Rearrange your command tokens", ""}}
                          : std::vector<Option>{{0, "Take back a command token", ""}, {1, "Rearrange your command tokens", ""}};
                break;
            case 6: opts = {{0, "Research 1 technology", ""}, {1, "Research a second one for 6 resources", ""}}; break;
            case 7: opts = {{0, "Score a public objective", ""}, {1, "1 VP for Mecatol Rex, or a secret objective", ""}}; break;
        }
        brikkClause_ = pick(p, Tag::Generic, std::string("Brikk Strain: resolve which part of the ") + strategyName(card) + " primary?", opts,
                            pn(G.blightOwner) + "'s Galactic Blight is on this card: you resolve only 1 clause of its primary ability.");
    }
    switch (card) {
        case 0: leadership(p, true); break;
        case 1: diplomacy(p, true); break;
        case 2:
            politics(p, true);
            if (G.modules & kModDiscordant) dsAfterPolitics(p);  // Contagion (Kyro)
            break;
        case 3: construction(p, true); break;
        case 4: trade(p, true); break;
        case 5: warfare(p, true); break;
        case 6: technology(p, true); break;
        case 7: imperial(p, true); break;
    }
}

void Rules::secondaryAbility(int q, int card) {
    Player& pl = G.players[q];
    if (card == 0) {  // Leadership costs no strategy token
        if (enervate(q, card)) return;  // Enervate (the Obsidian's plot): the primary instead
        const int before = G.players[size_t(q)].tactic + G.players[size_t(q)].fleet + G.players[size_t(q)].strategy;
        leadership(q, false);
        if ((G.modules & kModDiscordant) && G.players[size_t(q)].tactic + G.players[size_t(q)].fleet + G.players[size_t(q)].strategy > before)
            dsSecondaryDone(q, card);  // Fine Print (Vaden)
        return;
    }
    const bool ener = graceFree_ || enervateFree(q, card);  // Enervate (Grace, Discordant Stars): no command token
    const bool freeTrade = card == 4 && fac(q, "hacan");  // Masters of Trade: no token needed
    // Acquiescence (Winnu's note): no token for the secondary of the Winnu player's strategic action
    const int w = strategicPlayer_;
    // (fixes 26, the Winnu review) not for Mathis Mathinus' secondaries, and the original card (games without the Codex)
    // works at the end of the strategy phase instead (endOfStrategyPhase)
    const bool acq = !ener && w >= 0 && w != q && fac(w, "winnu") && holdsNote(G, q, w, kNoteFaction) &&
                     (G.fixes < 26 || (!mathis_ && codexOn(G)));
    // Hesh and Prit (Naaz-Rokha hero): tokens come from the reinforcements; Jae Mir Kan (Mahact agent): the active player's
    const bool canPay = heshPrit_ ? reinforcementTokens(q) > 0 : (pl.strategy > 0 || jaeMirKan(q, false));
    // Mindsieve (Naalu breakthrough, Thunder's Edge): give the card's owner a promissory note instead of a token
    const bool sieve = !ener && w >= 0 && w != q && breakthroughIs(G, q, "naalu") && !pl.notes.empty();
    if (!canPay && !freeTrade && !acq && !sieve && !ener) return;
    // Only offer secondaries that could do something.
    switch (card) {
        case 1: {
            bool any = false;
            for (int x : controlledPlanets(G, q))
                if (G.planets[x].exhausted) any = true;
            if (!any) return;
            break;
        }
        case 2: break;
        case 3: {
            bool any = false;
            for (int x : controlledPlanets(G, q)) {
                const Force& f = G.planets[x].units[q];
                if ((f.count(UnitType::PDS) < 2 || lawInPlay(G, "defense_act")) && unitsAvailable(G, q, UnitType::PDS) > 0) any = true;
                if (f.count(UnitType::SpaceDock) == 0 && unitsAvailable(G, q, UnitType::SpaceDock) > 0) any = true;
            }
            if (!any) return;
            break;
        }
        case 4:
            if (pl.commodities >= commodityCap(G, pl)) return;
            // 0.29.0 (owner): players the primary already refreshed (X-1 included) have used the
            // secondary; they aren't offered it again after sending their commodities away (new games)
            if ((G.extras & 8) && ((tradeChosen_ >> q) & 1)) return;
            break;
        case 5:
            if (productionCapacity(G, q, pl.homeSystem) <= 0) return;
            break;
        case 6: {
            bool compounds = false;  // Specialist Compounds (Jol-Nar breakthrough): a ready specialty planet pays
            if (breakthroughIs(G, q, "jolnar"))
                for (int x : controlledPlanets(G, q))
                    if (!G.planets[size_t(x)].exhausted && planetSkips(G, x)) compounds = true;
            // ((fixes 27, the Jol-Nar review - owner 2026-11-13) Brilliant: the primary it becomes costs nothing)
            const bool brilliant = G.fixes >= 27 && fac(q, "jolnar");
            if (spendable(G, q, false) < 4 && lawOwner(G, "minister_sciences") != q && !compounds && !brilliant) return;
            bool any = false;
            for (int t = 0; t < kTechCount && !any; ++t)
                if (canResearchNow(G, q, t)) any = true;
            if (!any) return;
            break;
        }
        case 7:
            if (G.secretDeck.empty()) return;
            break;
    }
    if (!forcedSecondary_ &&  // (Strategize, Thunder's Edge: already chosen)
        !yesNo(q, Tag::SecondaryUse, std::string("Use the secondary ability of ") + strategyName(card) + "?",
               std::string(strategySecondary(card)) + (freeTrade ? " (Masters of Trade: no command token needed.)" : ener ? " (Enervate: no command token needed.)" : "")))
        return;
    bool useAcq = false;
    if (acq && !freeTrade)
        useAcq = pl.strategy <= 0 || fyes(q, Ab::Acquiescence, "Play Acquiescence instead of spending a command token?",
                                          factionNoteText(G.players[w].faction, kNoteFaction));
    bool jae = false, sieved = false;
    if (sieve && !useAcq && !freeTrade && !heshPrit_) {
        std::vector<Option> opts;
        for (int16_t n : pl.notes)
            if (noteOwner(n) != w) opts.push_back({n, std::string("Give ") + noteNameOf(G, n), ""});
        if (!opts.empty()) {
            opts.push_back({-1, canPay ? "Spend a command token" : "Don't use the secondary", ""});
            const int n = fpick(q, Ab::BtNaalu, "Mindsieve: give " + pn(w) + " a promissory note instead of a command token?", opts);
            if (n >= 0) {
                giveNote(q, w, n);
                say(q, pn(q) + " uses Mindsieve: no command token needed.");
                sieved = true;
            }
        }
        if (!sieved && !canPay) return;
    }
    if (sieved) {
        // (paid with the note)
    } else if (useAcq) {
        returnNote(q, makeNote(w, kNoteFaction));
        say(q, pn(q) + " plays Acquiescence: no command token needed.");
    } else if (freeTrade || heshPrit_ || ener) {
        // no token from the strategy pool (Hesh and Prit: a token from the reinforcements)
    } else if (jaeMirKan(q, true)) {
        jae = true;  // (fixes 26, owner 2026-11-12: Construction places the active player's token instead - LRR 24.3)
        if (G.fixes >= 26) jaeToken_ = strategicPlayer_;
    } else if (!scepter(q)) {
        spendStrategy(q, 1);  // Scepter of Emelpar: from reinforcements instead
    }
    {
        Event e;
        e.kind = EventKind::Secondary;
        e.player = int8_t(q);
        e.card = int16_t(card);
        e.text = pn(q) + " uses the secondary ability of " + strategyName(card) + ".";
        E.emit(e);
    }
    const bool wasFree = freeSecondary_;
    if (useAcq || jae || sieved || ener) freeSecondary_ = true;
    struct JaeGuard {
        int& v;
        ~JaeGuard() { v = -1; }
    } jaeGuard{jaeToken_};
    switch (card) {
        case 1: diplomacy(q, false); break;
        case 2:
            politics(q, false);
            if (G.modules & kModDiscordant) dsAfterPolitics(q);  // Contagion (Kyro)
            break;
        case 3: construction(q, (G.modules & kModDiscordant) && dsConstructionPrimary(q)); break;  // (B-Unit 205643a: the primary)
        case 4: trade(q, false); break;
        case 5: warfare(q, (G.modules & kModDiscordant) && dsZealous(q)); break;  // Zealous (Kortali): the primary instead
        case 6:
            // Brilliant (Jol-Nar): resolve the primary ability instead.
            if (G.fixes >= 27) {
                // (fixes 27, the Jol-Nar review - owner 2026-11-13) "When you spend a command token to resolve the secondary
                // ability": not when a note (Mindsieve) or nothing (Enervate) paid; below 4 resources (and no other way to
                // pay) it is the only way to research, so it is used without asking
                bool brilliant = false;
                if (fac(q, "jolnar") && !sieved && !ener) {
                    bool compounds = false;
                    if (breakthroughIs(G, q, "jolnar"))
                        for (int x : controlledPlanets(G, q))
                            if (!G.planets[size_t(x)].exhausted && planetSkips(G, x)) compounds = true;
                    const bool canFollow = spendable(G, q, false) >= 4 || lawOwner(G, "minister_sciences") == q || compounds;
                    brilliant = !canFollow || fyes(q, Ab::Brilliant, "Brilliant: resolve Technology's primary ability instead?",
                                                   std::string(abilityText(G.players[size_t(q)].faction, 1)));
                    if (brilliant) say(q, pn(q) + " resolves Technology's primary ability instead (Brilliant).");
                }
                technology(q, brilliant);
            } else if (fac(q, "jolnar") && fyes(q, Ab::Brilliant, "Brilliant: resolve Technology's primary ability instead?",
                                                 "Research 1 technology for free, then a second one for 6 resources if you like."))
                technology(q, true);
            else
                technology(q, false);
            break;
        case 7: imperial(q, false); break;
        default: break;
    }
    freeSecondary_ = wasFree;
    if (G.modules & kModDiscordant) dsSecondaryDone(q, card);  // Fine Print (Vaden)
}

// ------------------------------------------------------------------ 1 Leadership
void Rules::leadership(int p, bool primary) {
    if (primary && brikkOk(0)) gainTokens(p, 3, false, "Leadership: gain 3 command tokens.");
    if (primary && !brikkOk(1)) return;
    int reinforcements = reinforcementTokens(p);
    int maxBuy = std::min(spendable(G, p, true) / 3, reinforcements);
    if (maxBuy <= 0) return;
    std::vector<Option> opts;
    opts.push_back({0, "None", ""});
    for (int k = 1; k <= maxBuy; ++k)
        opts.push_back({k, std::to_string(k) + " token" + (k > 1 ? "s" : ""), std::to_string(3 * k) + " influence"});
    int k = pick(p, Tag::Leadership, "Leadership: buy command tokens (3 influence each)?", opts);
    if (k <= 0) return;
    if (pay(p, 3 * k, true, "Leadership tokens", false)) gainTokens(p, k, false, "Leadership: tokens bought with influence.");
}

// ------------------------------------------------------------------ 2 Diplomacy
void Rules::readyPlanets(int p, int n) {
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (G.planets[x].exhausted)
            opts.push_back({x, planetName(x),
                            std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
    if (opts.empty()) return;
    std::vector<int> picks = pickMany(p, Tag::ReadyPlanets, "Ready up to " + std::to_string(n) + " planets", opts, 0,
                                      std::min<int>(n, int(opts.size())), "", true);
    for (int x : picks) G.planets[x].exhausted = false;
    if (!picks.empty()) say(p, pn(p) + " readies " + std::to_string(picks.size()) + " planet(s).");
}

void Rules::diplomacy(int p, bool primary) {
    if (primary && brikkOk(0)) {
        std::vector<Option> opts;
        int mec = G.mecatol();
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (int(s) == mec) continue;
            bool mine = false;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].owner == p) mine = true;
            if (mine) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (!opts.empty()) {
            int s = pick(p, Tag::DiplomacySystem, "Diplomacy: protect which system?", opts,
                         "Every other player places a command token there.", true);
            for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
                if (q == p || G.systems[s].hasToken(q)) continue;
                placeTokenFromReinforcements(q, s, pn(p) + " used Diplomacy: you must place a command token in " + sysName(s) + ".");
            }
            say(p, pn(p) + " uses Diplomacy on " + sysName(s) + ".", s);
        }
    }
    // RULES-CHECK: both the primary and secondary ready up to 2 exhausted planets.
    if (!primary || brikkOk(1)) readyPlanets(p, 2);
    afterDiplomacy(p);  // Peace Accords
}

// ------------------------------------------------------------------ 3 Politics
void Rules::politics(int p, bool primary) {
    if (!primary) {
        drawActionCards(p, 2, "Politics");
        return;
    }
    // LRR 66.2: new speaker, draw 2 action cards, look at the top 2 agendas.
    std::vector<Option> opts;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != G.speaker && !G.players[q].eliminated) opts.push_back({q, pn(q), ""});
    if (brikkOk(0)) {
        int s = pick(p, Tag::PoliticsSpeaker, "Politics: choose the new speaker", opts,
                     "The speaker picks strategy cards first and breaks tied votes.");
        G.speaker = int8_t(s);
        say(p, pn(s) + " is now the speaker.");
    }
    if (brikkOk(1)) drawActionCards(p, 2, "Politics");
    if (brikkOk(2)) politicsPeek(p);
}

// ------------------------------------------------------------------ 4 Construction
void Rules::placeStructure(int p, int sys, bool allowDock, bool allowPds, bool optional) {
    std::vector<Option> opts;
    const bool dockOk = allowDock && unitsAvailable(G, p, UnitType::SpaceDock) > 0;
    const bool pdsOk = allowPds && unitsAvailable(G, p, UnitType::PDS) > 0;
    const bool floating = unitDef(G, p, UnitType::SpaceDock).spaceOnly;
    for (int x : controlledPlanets(G, p)) {
        int s = G.planets[x].system;
        if (sys >= 0 && s != sys) continue;
        if (isDMZ(G, x)) continue;  // Demilitarized Zone
        const Force& f = G.planets[x].units[p];
        if (dockOk && !floating && f.count(UnitType::SpaceDock) == 0)
            opts.push_back({x * 2 + 1, std::string("Space dock on ") + planetName(x),
                            "Produces up to " + std::to_string(planetResources(G, x) + unitDef(G, p, UnitType::SpaceDock).production) +
                                " units."});
        if (pdsOk && unitDef(G, p, UnitType::PDS).spaceOnly) {  // Privileged Citizenry (Mirveda): Gauss Cannons, fleet tokens per system
            bool dup = false;
            for (const Option& o : opts) dup = dup || (o.id % 2 == 0 && o.id >= 0 && G.planets[size_t(o.id / 2)].system == s);
            if (!dup && G.systems[size_t(s)].space[size_t(p)].count(UnitType::PDS) < G.players[size_t(p)].fleet)
                opts.push_back({x * 2, std::string("Gauss Cannon in ") + sysName(s), "In the space area: space cannon and bombardment; it moves like a ship."});
        } else if (pdsOk && (f.count(UnitType::PDS) < 2 || lawInPlay(G, "defense_act")))
            opts.push_back({x * 2, std::string("PDS on ") + planetName(x), "Space cannon and planetary shield."});
    }
    if (dockOk && floating) {
        // Saar floating factories go in the space area of a system with your planet
        for (int x : controlledPlanets(G, p)) {
            int s = G.planets[x].system;
            if (sys >= 0 && s != sys) continue;
            bool dup = false;
            for (const Option& o : opts)
                if (o.id == -(s + 10)) dup = true;
            if (!dup) opts.push_back({-(s + 10), "Floating factory in " + sysName(s), ""});
        }
    }
    if (opts.empty()) return;
    if (optional) opts.push_back({-1, "Nothing", ""});
    int c = pick(p, Tag::ConstructionWhere, "Place a structure", opts, "", false, false);
    if (c == -1) return;
    if (c <= -10) {
        int s = -c - 10;
        addUnits(p, UnitType::SpaceDock, 1, s, -1);
        say(p, pn(p) + " places a floating factory in " + sysName(s) + ".", s);
        if (lawOwner(G, "minister_industry") == p) production(p, s, productionCapacity(G, p, s), false);
        return;
    }
    int planet = c / 2;
    UnitType t = (c % 2) ? UnitType::SpaceDock : UnitType::PDS;
    if (monumentInstead(p, planet)) return;  // Monuments to the Ages (galactic event)
    if (t == UnitType::PDS && hecatoncheires(p, planet)) return;  // Titans mech: a mech and an infantry instead
    addUnits(p, t, 1, G.planets[planet].system, planet);
    say(p, pn(p) + " builds a " + unitTypeName(t) + " on " + planetName(planet) + ".", G.planets[planet].system);
    if (t == UnitType::SpaceDock && (G.modules & kModDiscordant)) dsDockPlaced(p, planet);  // Industrious (Roh'Dhna)
    // Minister of Industry: a new space dock lets your units in that system produce.
    if (t == UnitType::SpaceDock && lawOwner(G, "minister_industry") == p)
        production(p, G.planets[planet].system, productionCapacity(G, p, G.planets[planet].system), false);
}

void Rules::construction(int p, bool primary) {
    // Thunder's Edge's revised card (0.34.0): primary - a structure OR one space dock's production, then a
    // structure; secondary - a strategy token for a structure (no token is placed on the board)
    if (G.modules & kModThundersEdge) {
        if (!primary) {
            placeStructure(p, -1, true, true, false);
            return;
        }
        std::vector<Option> docks;  // id: the planet with the dock, or 100000 + system for a floating factory
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (blockaded(G, p, int(s))) continue;  // (a blockaded dock can't produce ships; keep it simple)
            if (G.systems[s].space[size_t(p)].count(UnitType::SpaceDock) && unitDef(G, p, UnitType::SpaceDock).spaceOnly)
                docks.push_back({100000 + int(s), "Floating factory in " + sysName(int(s)), ""});
            for (int16_t x : G.systems[s].planets)
                if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock))
                    docks.push_back({x, std::string("Space dock on ") + planetName(x), sysName(int(s))});
        }
        int c = 1;
        if (!brikkOk(0)) docks.clear(), c = 0;
        if (!docks.empty())
            c = pick(p, Tag::ConstructionChoice, "Construction: place a structure, or produce at one of your space docks?",
                     {{1, "Place 1 structure", "A PDS or a space dock on a planet you control."},
                      {2, "Produce at 1 space dock", "Use its Production ability now (paid as usual)."}},
                     "Then place 1 structure.");
        if (c == 1) {
            placeStructure(p, -1, true, true, true);
        } else if (c == 2) {
            const int d = docks.size() == 1 ? docks[0].id : pick(p, Tag::ConstructionDock, "Construction: produce at which space dock?", docks);
            const UnitDef& dock = unitDef(G, p, UnitType::SpaceDock);
            const int s = d >= 100000 ? d - 100000 : G.planets[size_t(d)].system;
            const int cap = dock.production + (d < 100000 && dock.productionPlusResources ? planetResources(G, d) : 0);
            production(p, s, cap, false);
        }
        if (brikkOk(1)) placeStructure(p, -1, true, true, true);
        return;
    }
    if (primary) {
        // LRR 24.2: may place 1 PDS or 1 space dock, then may place 1 more PDS.
        if (brikkOk(0)) placeStructure(p, -1, true, true, true);
        if (brikkOk(1)) placeStructure(p, -1, false, true, true);
        return;
    }
    // Secondary (LRR 24.3): the strategy token goes into any system (back to
    // reinforcements if you already have one there), then a structure on a
    // planet you control in that system.
    std::vector<Option> opts;
    const bool floating = unitDef(G, p, UnitType::SpaceDock).spaceOnly;
    for (int x : controlledPlanets(G, p)) {
        int s = G.planets[x].system;
        const Force& f = G.planets[x].units[p];
        bool canPlace = ((f.count(UnitType::PDS) < 2 || lawInPlay(G, "defense_act")) && unitsAvailable(G, p, UnitType::PDS) > 0) ||
                        ((floating || f.count(UnitType::SpaceDock) == 0) && unitsAvailable(G, p, UnitType::SpaceDock) > 0);
        if (!canPlace) continue;
        bool dup = false;
        for (const Option& o : opts)
            if (o.id == s) dup = true;
        if (!dup)
            opts.push_back({s, sysName(s),
                            G.systems[s].hasToken(p) ? "You already have a token here: the spent token returns to your reinforcements."
                                                     : "Your strategy token stays here: you can't activate this system this round."});
    }
    if (opts.empty()) return;
    // (fixes 26, the Mahact review - owner 2026-11-12) LRR 24.3: "spend one command token ... and place that command token
    // in any system" - with Jae Mir Kan the token used is the active player's, so theirs goes there
    const int jt = G.fixes >= 26 ? jaeToken_ : -1;
    if (jt >= 0)
        for (Option& o : opts)
            o.detail = G.systems[size_t(o.id)].hasToken(jt) ? pn(jt) + " already has a token here: theirs goes back to their reinforcements."
                                                            : pn(jt) + "'s command token (Jae Mir Kan) goes here: they can't activate this system this round.";
    int s = pick(p, Tag::ConstructionSystem,
                 jt >= 0 ? "Construction: put " + pn(jt) + "'s command token in which system?" : std::string("Construction: put your strategy token in which system?"),
                 opts, "Then place a PDS or space dock on a planet you control there.", true);
    if (jt >= 0) {
        if (!G.systems[size_t(s)].hasToken(jt)) {
            G.systems[size_t(s)].tokens |= uint16_t(1u << jt);
            zeuTE(jt, s);  // (fixes 27)
        }
        say(p, pn(p) + " places " + pn(jt) + "'s command token in " + sysName(s) + " (Jae Mir Kan).", s);
    }
    // no token was spent (Galactic Crisis Pact, Acquiescence): none is placed either
    else if (!freeSecondary_ && !G.systems[s].hasToken(p)) {
        G.systems[s].tokens |= uint16_t(1u << p);
        zeuTE(p, s);  // (fixes 27)
    }
    placeStructure(p, s, true, true, false);
}

// ------------------------------------------------------------------ 5 Trade
void Rules::trade(int p, bool primary) {
    Player& pl = G.players[p];
    if (primary && brikkOk(0)) {
        gainTG(p, 3);
        say(p, pn(p) + " gains 3 trade goods.");
    }
    if (!primary || brikkOk(1)) {
        const int before = pl.commodities;
        pl.commodities = int8_t(refreshTo(G, pl));
        say(p, pn(p) + " refills commodities to " + std::to_string(pl.commodities) + ".");
        replenished(p, pl.commodities - before);
    }
    if (!primary || !brikkOk(2)) return;
    // (Discordant Stars: the commodity value with its modifiers - C.M.O. Ranc, Exploit, Necrophage)
    // (0.45.0, fixes 14: always with the modifiers - Thunder's Edge space stations too)
    auto maxOf = [&](int q) {
        return (G.modules & kModDiscordant) || G.fixes >= 14 ? commodityValue(G, q) : int(kFactions[G.players[q].faction].commodities);
    };
    auto room = [&](int q) {  // (older saves: the old list, eliminated players included)
        return q != p && (G.tradeRules == 0 || !G.players[q].eliminated) && (G.players[q].commodities < maxOf(q) || eventOn(G, kEvCommerce));
    };
    auto refill = [&](int q, const std::string& why) {
        const int before = G.players[q].commodities;
        G.players[q].commodities = int8_t(eventOn(G, kEvCommerce) ? refreshTo(G, G.players[q]) : maxOf(q));
        say(q, pn(q) + " refills commodities" + why + ".");
        replenished(q, G.players[q].commodities - before);
    };
    // House rule (owner, 0.14.0): in round 1 everyone refills for free - a
    // gentleman's agreement to start even and get the trading going. Since
    // 0.15.0 (bit 2, RULES-CHECK 0.14.0 #4) only when a computer player holds
    // Trade: a human chooses as usual.
    tradeChosen_ = 0;  // 0.29.0: who the primary refreshed (they have used the secondary - LRR: "without spending a token")
    if ((G.tradeRules & 2) && G.round == 1 && !((G.tradeRules & 4) && G.players[p].human)) {
        for (int q = 0; q < G.numPlayers; ++q)
            if (room(q)) {
                refill(q, " (round 1: free for everyone)");
                tradeChosen_ |= 1u << q;
            }
        return;
    }
    // A human holding Trade in round 1 under the house rule picks whom to refill
    // (no X-1 offers yet: the gentleman's agreement is a free round).
    const bool round1Choice = (G.tradeRules & 2) && G.round == 1;
    std::vector<int> done;
    if ((G.tradeRules & 8) && !round1Choice) {
        tradeX1(p, done);  // 0.17.0: the community X-1 (rules_deals.cpp)
    } else if ((G.tradeRules & 1) && !round1Choice) {
        // X-1 trades (as players do at the table): the player refills for free, then
        // sends back all of the new commodities but one - they become our trade goods.
        // It is a transaction, so only with players we may trade with now (LRR 94).
        std::vector<Option> xo;
        for (int q = 0; q < G.numPlayers; ++q)
            if (room(q) && maxOf(q) >= 2 && transactionAllowed(G, p, q))
                xo.push_back({q, pn(q),
                              "Refills to " + std::to_string(maxOf(q)) + ", sends you " + std::to_string(maxOf(q) - 1) + " (keeps 1)."});
        if (!xo.empty())
            for (int q : pickMany(p, Tag::TradeX1Offer, "Trade: offer X-1 trades?", xo, 0, int(xo.size()),
                                  "They refill for free and send you all their commodities but one: those become your trade goods.")) {
                const int x = maxOf(q);
                Decision d;
                d.player = q;
                d.kind = DecisionKind::Pick;
                d.tag = Tag::TradeX1Accept;
                d.targetPlayer = p;
                d.prompt = pn(p) + " offers an X-1 trade";
                d.context = "Refill your commodities to " + std::to_string(x) + " for free, then send " + std::to_string(x - 1) + " of them to " +
                            pn(p) + " (they become " + pn(p) + "'s trade goods). You keep 1.";
                d.options = {{1, "Accept", "Keep 1 commodity, no strategy token spent."}, {0, "Decline", ""}};
                d.validate = [](const Answer& a) { return a.picks.size() == 1 && (a.picks[0] == 0 || a.picks[0] == 1) ? "" : "Choose one."; };
                const int yes = E.ask(std::move(d)).picks[0];
                Deal deal;
                deal.from = p;
                deal.to = q;
                deal.get.comm = x - 1;
                if (yes == 1) {
                    refill(q, " (X-1 trade with " + pn(p) + ")");
                    if (checkDeal(G, deal).empty() && executeDeal(deal)) recordDeal(deal, 1);
                    else recordDeal(deal, 3);
                    done.push_back(q);
                } else {
                    recordDeal(deal, 2);
                    persDealRefused(deal);
                    Event ev;
                    ev.kind = EventKind::Deal;
                    ev.card = int16_t(kDealDeclined);
                    ev.player = int8_t(q);
                    ev.other = int8_t(p);
                    ev.text = pn(q) + " declines " + pn(p) + "'s X-1 trade.";
                    E.emit(std::move(ev));
                }
            }
    }
    for (int q : done) tradeChosen_ |= 1u << q;
    std::vector<Option> opts;
    for (int q = 0; q < G.numPlayers; ++q)
        if (room(q) && std::find(done.begin(), done.end(), q) == done.end())
            opts.push_back({q, pn(q), "Refill their commodities without spending a strategy token."});
    if (opts.empty()) return;
    for (int q : pickMany(p, Tag::TradeReplenish, "Trade: let other players refill their commodities?", opts, 0, int(opts.size()),
                          round1Choice ? "Round 1 house rule: a computer player with Trade refills everyone; you choose as usual." : "")) {
        const int before = G.players[q].commodities;
        G.players[q].commodities = int8_t(eventOn(G, kEvCommerce) || (G.modules & kModDiscordant) || G.fixes >= 14 ? refreshTo(G, G.players[q])
                                                                                                             : kFactions[G.players[q].faction].commodities);
        say(q, pn(q) + " refills commodities (thanks to " + pn(p) + ").");
        replenished(q, G.players[q].commodities - before);
        tradeChosen_ |= 1u << q;
    }
}

// After a player replenishes commodities: Minister of Commerce, Trade Agreement.
void Rules::replenished(int p, int gained) {
    siphon(p, gained);  // Siphon (the Obsidian's plot)
    if (lawOwner(G, "minister_commerce") == p) {
        int n = 0;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && areNeighbours(G, p, q)) ++n;
        gainTG(p, n);
        if (n) say(p, pn(p) + " gains " + std::to_string(n) + " trade goods (Minister of Commerce).");
    }
    tradeAgreement(p);
    replenished4(p);  // The Stillness of Stars
    if (G.modules & kModDiscordant) dsReplenished(p);  // Military Industrial Complex (Axis)
}

// ------------------------------------------------------------------ 6 Warfare
void Rules::warfare(int p, bool primary) {
    // Thunder's Edge's revised card (0.34.0): primary - a tactical action in any system without a command token
    // (it still counts as activated), tokens redistributed before and after; secondary - a strategy token for the
    // Production of the units in your home system (no token is placed there)
    if (G.modules & kModThundersEdge) {
        if (primary) {
            if (brikkOk(1)) gainTokens(p, 0, true, "Warfare: rearrange your command tokens before the tactical action.");
            if (brikkOk(0)) {
                warfareTac_ = true;
                tacticalAction(p);
                warfareTac_ = false;
            }
            if (!G.players[size_t(p)].eliminated && brikkOk(1) && brikkClause_ < 0) gainTokens(p, 0, true, "Warfare: rearrange your command tokens after it.");
            return;
        }
        const int home = G.players[size_t(p)].homeSystem;
        if (home >= 0 && productionCapacity(G, p, home) > 0) production(p, home, productionCapacity(G, p, home), true);
        return;
    }
    if (primary) {
        // LRR 99.1-99.2: take one of your command tokens back from the board into a
        // pool, then you may redistribute your tokens.
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
        int regained = 0;
        if (!brikkOk(0)) opts.clear();
        if (!opts.empty()) {
            int s = pick(p, Tag::WarfareToken, "Warfare: take back a command token from which system?", opts, "", true);
            G.systems[s].tokens &= uint16_t(~(1u << p));
            say(p, pn(p) + " takes back the command token in " + sysName(s) + ".", s);
            regained = 1;
        }
        if (brikkOk(1)) gainTokens(p, regained, true,
                                   regained ? "Warfare: put the returned token in a pool and rearrange your tokens." : "Warfare: rearrange your tokens.");
        else if (regained) gainTokens(p, regained, false, "Warfare: the returned command token.");
        return;
    }
    const Player& pl = G.players[p];
    int home = pl.homeSystem;
    // one space dock in the home system
    std::vector<Option> opts;
    for (int16_t x : G.systems[home].planets)
        if (G.planets[x].units[p].count(UnitType::SpaceDock))
            opts.push_back({x, planetName(x),
                            "Up to " + std::to_string(planetResources(G, x) + unitDef(G, p, UnitType::SpaceDock).production) + " units"});
    if (G.systems[home].space[p].count(UnitType::SpaceDock))
        opts.push_back({-1, "Floating factory", "Up to " + std::to_string(unitDef(G, p, UnitType::SpaceDock).production) + " units"});
    if (opts.empty()) return;
    int c = opts.size() == 1 ? opts[0].id : pick(p, Tag::WarfareDock, "Warfare: produce at which space dock?", opts);
    int cap = c >= 0 ? planetResources(G, c) + unitDef(G, p, UnitType::SpaceDock).production
                     : unitDef(G, p, UnitType::SpaceDock).production;
    production(p, home, cap, true);
}

// ------------------------------------------------------------------ 7 Technology
bool Rules::researchStep(int p, int resourceCost, const std::string& why, int exclude) {
    if (fac(p, "nekro")) return propagation(p, resourceCost);  // Propagation: command tokens instead
    const int cost0 = resourceCost;  // (fixes 27: a second pick when the Deepwrought say no)
    std::vector<Option> opts;
    std::vector<int> inheritOnly;  // reachable only with Inheritance Systems (L1Z1X)
    std::vector<int> inheritMaybe;  // (0.44.9) reachable without it - it may still be used
    const bool inherit = techReady(p, "is");
    const bool aida = techReady(p, "aida");
    auto shortBy = [&](int t) {  // prerequisites the planets can't cover, after Analytical
        int missing[4], extra = 0;
        missingPrereqs(G, p, t, missing);
        for (int c = 0; c < 4; ++c) extra += std::max(0, missing[c] - int(skipPlanets(G, p, TechColor(c)).size()));
        return extra - analyticalWaiver(G, p, t);
    };
    auto aidaFits = [&](int t) {
        // AI Development Algorithm: ignore 1 prerequisite of a unit upgrade.
        if (!aida || !kTechs[t].unitUpgrade || !techAvailableToResearch(G, p, t)) return false;
        return shortBy(t) == 1;
    };
    // The Prophet's Tears: ignore 1 prerequisite of any technology.
    const bool tears = relicReady(p, "prophetstears");
    auto tearsFits = [&](int t) {
        if (!tears || !techAvailableToResearch(G, p, t)) return false;
        return shortBy(t) == 1;
    };
    // Riftmeld (Vuil'raith): give back a captured unit of that type to ignore every prerequisite of a unit upgrade
    auto riftmeldFits = [&](int t) {
        if (!fac(p, "cabal") || !kTechs[t].unitUpgrade || !techAvailableToResearch(G, p, t)) return false;
        for (int u = 0; u < kUnitCount; ++u)
            if (kUnits[u].tech == t) return capturedCount(G, p, kUnits[u].type) > 0;
        return false;
    };
    // Brother Omar Omega (Yin commander, Codex 0.31.0): a technology another player owns, its prerequisites ignored
    // by returning 1 infantry to the reinforcements
    auto omarFits = [&](int t) {
        if (!codexOn(G) || !commanderUsable(G, p, "yin") || !techAvailableToResearch(G, p, t)) return false;
        if (unitsOnBoard(G, p, UnitType::Infantry) <= 0) return false;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.players[q].hasTech(t)) return true;
        return false;
    };
    // Forbidden Knowledge (Uncharted Space action card): ignore 1 prerequisite (2 if not a unit upgrade)
    const bool fk = fkReady(p);
    auto fkFits = [&](int t) {
        if (!fk || !techAvailableToResearch(G, p, t)) return false;
        const int s = shortBy(t);
        return s >= 1 && s <= (kTechs[t].unitUpgrade ? 1 : 2);
    };
    // Doctor Carrina (Deepwrought agent, Thunder's Edge): the Deepwrought may let us ignore 1 prerequisite
    const int dwAgent = (G.modules & kModThundersEdge) ? playerOfFaction(G, "deepwrought") : -1;
    const bool carrinaOk = dwAgent >= 0 && dwAgent != p && canUseAgent(dwAgent, "deepwrought") && !carrinaNo_;
    auto carrinaFits = [&](int t) { return carrinaOk && techAvailableToResearch(G, p, t) && shortBy(t) == 1; };
    for (int t = 0; t < kTechCount; ++t) {
        if (t == exclude || !techAvailableToResearch(G, p, t)) continue;
        if (onlyGeneric_ && kTechs[t].faction >= 0) continue;  // Visionaria Select: a generic technology
        if (onlyGeneric_ && G.fixes >= 8 && kTechs[t].unitUpgrade) continue;  // (0.44.4: ... and not a unit upgrade)
        // (Thunder's Edge factions' own technologies; the Obsidian's can't be researched)
        const bool teOwn = (kTechs[t].source == Source::ThundersEdge && kTechs[t].faction >= 0 &&
                            std::strcmp(kFactions[kTechs[t].faction].key, "obsidian") != 0) ||
                           (kTechs[t].source == Source::Discordant && kTechs[t].faction >= 0);  // (Discordant Stars factions' own)
        if (kTechs[t].source != Source::Base && kTechs[t].source != Source::PoK && !teOwn) continue;
        if (onlyColour_ >= 0 && (kTechs[t].unitUpgrade || int(kTechs[t].color) != onlyColour_)) continue;  // Specialist Compounds
        if (onlyUpgradeTypes_ && !(upgradeUnitType(t) >= 0 && ((onlyUpgradeTypes_ >> upgradeUnitType(t)) & 1))) continue;  // Glory (Kjalengard)
        if (onlyColours_ && !(kTechs[t].color != TechColor::None && ((onlyColours_ >> int(kTechs[t].color)) & 1))) continue;  // Logic Machina
        bool ok = canResearchNow(G, p, t) || aidaFits(t) || tearsFits(t) || riftmeldFits(t) || omarFits(t) || fkFits(t) || carrinaFits(t);
        if (!ok && inherit && spendable(G, p, false) >= resourceCost + 2) {
            // Inheritance Systems: exhaust it and pay 2 resources to ignore every prerequisite
            inheritOnly.push_back(t);
            std::string col = kTechs[t].unitUpgrade ? "Unit upgrade" : colorName(kTechs[t].color);
            opts.push_back({t, kTechs[t].name, col + " (uses Inheritance Systems: +2 resources). " + techSummary(t)});
            continue;
        }
        // (0.44.9, fixes 13 - the owner's answer C11) Inheritance Systems is offered for any research with prerequisites
        // still to cover, not only when nothing else works (it saves specialty planets and one-shot waivers)
        if (ok && inherit && G.fixes >= 13 && spendable(G, p, false) >= resourceCost + 2) {
            int m[4];
            missingPrereqs(G, p, t, m);
            if (m[0] + m[1] + m[2] + m[3] > 0) inheritMaybe.push_back(t);
        }
        if (ok && resourceCost > 0 && !owns(p, "pa")) {
            // skip planets used for prerequisites can't also pay (Psychoarchaeology: they aren't exhausted -
            // 0.24.0: an exhausted specialty planet used to count against the payment and hid the technology)
            int missing[4];
            missingPrereqs(G, p, t, missing);
            int lost = 0;
            for (int c = 0; c < 4; ++c) {
                std::vector<int> sp = skipPlanets(G, p, TechColor(c));
                for (int i = 0; i < missing[c] && i < int(sp.size()); ++i) lost += planetResources(G, sp[i]);
            }
            if (spendable(G, p, false) - lost < resourceCost) ok = false;
        }
        if (!ok) continue;
        std::string col = kTechs[t].unitUpgrade ? "Unit upgrade" : colorName(kTechs[t].color);
        if (!canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && !riftmeldFits(t) && !omarFits(t) && !fkFits(t) && carrinaFits(t))
            col += " (asks the Deepwrought's Doctor Carrina)";
        else if (!canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && !riftmeldFits(t) && !omarFits(t) && fkFits(t))
            col += " (plays Forbidden Knowledge)";
        else if (!canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && !riftmeldFits(t) && omarFits(t)) col += " (uses Brother Omar)";
        else if (!canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t)) col += " (uses Riftmeld)";
        else if (!canResearchNow(G, p, t) && !aidaFits(t)) col += " (uses the Prophet's Tears)";
        else if (!canResearchNow(G, p, t)) col += " (exhausts AI Development Algorithm)";  // (0.44.8)
        opts.push_back({t, kTechs[t].name, col + ". " + techSummary(t)});
    }
    if (researchDry_) return !opts.empty();  // (0.43.4: N'orr Supremacy asks first whether there is anything)
    if (opts.empty()) {
        say(p, pn(p) + " has nothing to research right now.");
        return false;
    }
    if (resourceCost > 0) opts.push_back({-1, "Don't research", ""});
    int t = pick(p, Tag::Research, "Research a technology" + (why.empty() ? std::string() : " (" + why + ")"), opts);
    if (t < 0) return false;
    int missing[4];
    missingPrereqs(G, p, t, missing);
    bool inheritNow = std::find(inheritOnly.begin(), inheritOnly.end(), t) != inheritOnly.end();
    if (!inheritNow && std::find(inheritMaybe.begin(), inheritMaybe.end(), t) != inheritMaybe.end()) {
        int need = 0;
        for (int c = 0; c < 4; ++c) need += missing[c];
        inheritNow = yesNo(p, Tag::UseTech,
                           "Exhaust Inheritance Systems and spend 2 more resources to ignore every prerequisite of " + std::string(kTechs[t].name) + "?",
                           std::to_string(need) + " prerequisite" + (need == 1 ? "" : "s") +
                               " to cover otherwise (specialty planets or other waivers).");
    }
    if (inheritNow) {
        exhaustTech(p, "is");
        say(p, pn(p) + " exhausts Inheritance Systems to ignore every prerequisite.");
        for (int c = 0; c < 4; ++c) missing[c] = 0;
        resourceCost += 2;
    }
    // Analytical (Jol-Nar): ignore 1 prerequisite - first one the planets can't cover, else save a planet.
    // (Iconoclasm, Lanefir: the same with a relic fragment; the other Discordant Stars waivers come below)
    if (!kTechs[t].unitUpgrade && (fac(p, "jolnar") || (fac(p, "lanefir") && fragmentCount(G, p) > 0))) {
        int pickC = -1;
        for (int c = 0; c < 4; ++c)
            if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) pickC = c;
        for (int c = 0; c < 4 && pickC < 0; ++c)
            if (missing[c] > 0) pickC = c;
        if (pickC >= 0) --missing[pickC];
        // Iconoclasm (Lanefir, Discordant Stars): the waiver costs a relic fragment
        if (pickC >= 0 && fac(p, "lanefir")) {
            purgeFragments(p, 1, -1, "Iconoclasm");
            say(p, pn(p) + " ignores a prerequisite of " + kTechs[t].name + " (Iconoclasm).");
        }
    }
    // (0.44.9, fixes 13 - the owner's answer W14) AI Development Algorithm readies every round: for a unit upgrade it
    // goes before a Discordant Stars waiver (Favor of Rhodun, Strain Dispersal, Bishop Ulin) that would be spent -
    // asked, so a player can keep it for a production instead
    bool aidaFirst = false;
    if (G.fixes >= 13 && aida && (G.modules & kModDiscordant) && kTechs[t].unitUpgrade && dsWaivers(p, t, false) > 0) {
        int pickC = -1;
        for (int c = 0; c < 4 && pickC < 0; ++c)
            if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) pickC = c;
        if (pickC >= 0 && fyes(p, Ab::AiDevAlgorithm,
                               "Exhaust AI Development Algorithm for the missing " + std::string(colorName(TechColor(pickC))) +
                                   " prerequisite (instead of your other waiver)?",
                               "It readies again in the status phase; the other waiver would be used up.")) {
            --missing[pickC];
            exhaustTech(p, "aida");
            aidaFirst = true;
            say(p, pn(p) + " uses AI Development Algorithm to ignore a prerequisite.");
        }
    }
    // Favor of Rhodun / Bishop Ulin (Discordant Stars): more prerequisites ignored when the planets can't cover them
    if (G.modules & kModDiscordant)
        for (int w = dsWaivers(p, t, false); w > 0; --w) {
            int pickC = -1;
            for (int c = 0; c < 4; ++c)
                if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) pickC = c;
            if (pickC < 0) break;
            --missing[pickC];
            dsWaivers(p, t, true);
        }
    if (G.modules & kModDiscordant) {  // Sdallari Tvungovot (Kjalengard commander): unit upgrades cover shared colours
        int before[4];
        for (int c = 0; c < 4; ++c) before[c] = missing[c];
        int left[4];  // only what the planets can't cover
        for (int c = 0; c < 4; ++c) left[c] = std::max(0, missing[c] - int(skipPlanets(G, p, TechColor(c)).size()));
        if (const int n = upgradeWaiversApply(G, p, t, left)) {
            for (int c = 0; c < 4; ++c) missing[c] = std::max(0, missing[c] - (std::max(0, before[c] - int(skipPlanets(G, p, TechColor(c)).size())) - left[c]));
            say(p, pn(p) + "'s unit upgrades cover " + std::to_string(n) + " prerequisite" + (n > 1 ? "s" : "") + " of " + kTechs[t].name + " (Sdallari Tvungovot).");
        }
    }
    bool usedTears = false, usedRiftmeld = false;
    {
        int short_ = 0;
        for (int c = 0; c < 4; ++c) short_ += missing[c];
        if (short_ > 0 && riftmeldFits(t)) {
            const bool must = !canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t);
            if ((must || fyes(p, Ab::Riftmeld, std::string("Riftmeld: give back a captured unit to ignore the prerequisites of ") + kTechs[t].name + "?",
                              abilityText(G.players[p].faction, 2))) &&
                riftmeld(p, t)) {
                for (int c = 0; c < 4; ++c) missing[c] = 0;
                usedRiftmeld = true;
            }
        }
    }
    if (!usedRiftmeld && !canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && !omarFits(t) && fkFits(t)) {
        // Forbidden Knowledge: play the card (Sabotage can still stop it - then nothing is researched)
        int card = -1;
        for (int16_t c : G.players[p].actionCards)
            if (!std::strcmp(kActionCards[c].key, "forbidden_knowledge")) card = c;
        WinCtx w;
        w.w = Win::Research;
        w.a = p;
        if (card < 0 || !playActionCard(p, card, &w)) return false;
        int waive = kTechs[t].unitUpgrade ? 1 : 2;
        for (int c = 0; c < 4 && waive > 0; ++c)
            while (waive > 0 && missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) {
                --missing[c];
                --waive;
            }
        usedRiftmeld = true;  // (nothing else needed)
    }
    if (!usedRiftmeld && !canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && omarFits(t)) {
        // Brother Omar Omega: an infantry goes back, the prerequisites are ignored
        int best = -1, bestN = 0, bestSys = -1;
        for (size_t x = 0; x < G.planets.size(); ++x)
            if (G.planets[x].units[p].count(UnitType::Infantry) > bestN) {
                bestN = G.planets[x].units[p].count(UnitType::Infantry);
                best = int(x);
            }
        if (best < 0)
            for (size_t s2 = 0; s2 < G.systems.size() && bestSys < 0; ++s2)
                if (G.systems[s2].space[p].count(UnitType::Infantry)) bestSys = int(s2);
        // (fixes 26, the Yin review - owner 2026-11-12) "you may return 1 of your infantry": a person chooses which (bots keep
        // taking it from their planet with the most)
        if (G.fixes >= 26 && G.players[size_t(p)].human) {
            std::vector<Option> io;
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (const int n = G.planets[x].units[p].count(UnitType::Infantry))
                    io.push_back({int(x), planetName(int(x)), std::to_string(n) + " infantry"});
            for (size_t s2 = 0; s2 < G.systems.size(); ++s2)
                if (const int n = G.systems[s2].space[p].count(UnitType::Infantry))
                    io.push_back({20000 + int(s2), "Space: " + sysName(int(s2)), std::to_string(n) + " infantry"});
            if (io.size() > 1) {
                const int w = fpick(p, Ab::YinCommander, "Brother Omar: return which infantry to your reinforcements?", io,
                                    "Its prerequisites are ignored for " + std::string(kTechs[t].name) + ".", false, true);
                best = w < 20000 ? w : -1;
                bestSys = w >= 20000 ? w - 20000 : -1;
            }
        }
        removing_ = true;
        if (best >= 0) destroyUnits(p, UnitType::Infantry, 1, G.planets[best].system, best, -1, "Brother Omar");
        else if (bestSys >= 0) destroyUnits(p, UnitType::Infantry, 1, bestSys, -1, -1, "Brother Omar");
        removing_ = false;
        for (int c = 0; c < 4; ++c) missing[c] = 0;
        usedRiftmeld = true;  // (nothing else needed)
        say(p, pn(p) + " returns an infantry to ignore the prerequisites (Brother Omar).");
    }
    if (!usedRiftmeld && !canResearchNow(G, p, t) && !aidaFits(t) && !tearsFits(t) && !omarFits(t) && !fkFits(t) && carrinaFits(t)) {
        if (!carrina(p)) {  // (the Deepwrought said no)
            // (fixes 27, the Deepwrought review: the research isn't lost - the player chooses another technology, as
            // with any choice that turns out impossible; before, 3.5-4 researches a game were lost in sims)
            if (G.fixes >= 27 && !carrinaNo_) {
                say(p, "The Deepwrought won't help: " + pn(p) + " chooses another technology.");
                carrinaNo_ = true;
                const bool ok = researchStep(p, cost0, why, exclude);
                carrinaNo_ = false;
                return ok;
            }
            say(p, "The Deepwrought won't help: " + pn(p) + " can't research " + kTechs[t].name + " now.");
            return false;
        }
        for (int c = 0; c < 4; ++c)
            if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) {
                --missing[c];
                break;
            }
        usedRiftmeld = true;
        say(p, pn(p) + " ignores a prerequisite with Doctor Carrina's help.");
    }
    if (usedRiftmeld || aidaFirst) {
        // nothing else needed
    } else if (!canResearchNow(G, p, t) && !aidaFits(t) && tearsFits(t)) {
        for (int c = 0; c < 4; ++c)
            if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) {
                --missing[c];
                break;
            }
        exhaustRelic(p, "prophetstears");
        usedTears = true;
        say(p, pn(p) + " exhausts the Prophet's Tears to ignore a prerequisite.");
    } else if ((G.extras & 16) && aida && kTechs[t].unitUpgrade && canResearchNow(G, p, t) && !owns(p, "pa")) {
        // 0.30.0 (owner): the specialty planets can cover the prerequisite, but AI Development Algorithm
        // may do it instead and keep that planet ready to pay
        int c = -1;
        for (int k = 0; k < 4 && c < 0; ++k)
            if (missing[k] > 0 && !skipPlanets(G, p, TechColor(k)).empty()) c = k;
        if (c >= 0) {
            const std::vector<int> sp = skipPlanets(G, p, TechColor(c));
            if (fyes(p, Ab::AiDevAlgorithm,
                     "Exhaust AI Development Algorithm to ignore 1 " + std::string(colorName(TechColor(c))) + " prerequisite (keeps " +
                         planetName(sp[0]) + " ready)?",
                     "Otherwise a " + std::string(colorName(TechColor(c))) + " specialty planet is exhausted for it.")) {
                --missing[c];
                exhaustTech(p, "aida");
                say(p, pn(p) + " uses AI Development Algorithm to ignore a prerequisite.");
            }
        }
    } else if (!canResearchNow(G, p, t) && aidaFits(t)) {
        // spend AI Development Algorithm on the prerequisite the planets can't cover
        for (int c = 0; c < 4; ++c)
            if (missing[c] > int(skipPlanets(G, p, TechColor(c)).size())) {
                --missing[c];
                break;
            }
        exhaustTech(p, "aida");
        say(p, pn(p) + " uses AI Development Algorithm to ignore a prerequisite.");
    }
    // (fixes 27, the Deepwrought review - owner 2026-11-14: "closest with the rules, but makes sense to implement") Doctor
    // Carrina also when the player could research without her: "You can ignore a prerequisite even if you can already meet
    // it" (the card's notes) - offered when it keeps a specialty planet ready (otherwise ignoring one changes nothing)
    if (G.fixes >= 27 && (G.modules & kModThundersEdge) && !usedRiftmeld && !usedTears && !aidaFirst && !carrinaNo_ && !owns(p, "pa")) {
        int c = -1;
        for (int k = 0; k < 4 && c < 0; ++k)
            if (missing[k] > 0 && !skipPlanets(G, p, TechColor(k)).empty()) c = k;
        if (c >= 0 && carrinaSpare(p, t)) {
            --missing[c];
            say(p, pn(p) + " ignores a prerequisite with Doctor Carrina's help.");
        }
    }
    TechColor synA, synB;
    const bool syn = synergyOf(G, p, synA, synB);  // Thunder's Edge: the pair's specialties stand in for each other
    for (int c = 0; c < 4; ++c) {
        if (!missing[c]) continue;
        std::vector<int> sp = skipPlanets(G, p, TechColor(c));
        if (syn && (c == int(synA) || c == int(synB)) && int(sp.size()) < missing[c])
            for (int x : skipPlanets(G, p, c == int(synA) ? synB : synA))
                if (std::find(sp.begin(), sp.end(), x) == sp.end()) sp.push_back(x);
        if (allSynergy(G, p) && int(sp.size()) < missing[c])  // The Quantumcore: any colour
            for (int o = 0; o < 4; ++o)
                if (o != c)
                    for (int x : skipPlanets(G, p, TechColor(o)))
                        if (std::find(sp.begin(), sp.end(), x) == sp.end()) sp.push_back(x);
        std::vector<int> chosen;
        if (owns(p, "pa")) {  // Psychoarchaeology: nothing is exhausted, no need to choose
            say(p, pn(p) + " uses " + std::to_string(missing[c]) + " " + colorName(TechColor(c)) +
                       " specialt" + (missing[c] > 1 ? "ies" : "y") + " (Psychoarchaeology).");
            continue;
        }
        bool twice = false;  // a planet with two specialties of this colour is in the list twice (Thunder's Edge)
        for (size_t i = 1; i < sp.size(); ++i)
            if (sp[i] == sp[i - 1]) twice = true;
        if (int(sp.size()) <= missing[c]) {  // (fewer: a waiver - Analytical, ... - covers the rest)
            chosen = sp;
        } else if (twice) {  // chosen for the player: the double planet first (one planet, two prerequisites)
            std::vector<int> order = sp;
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                return std::count(sp.begin(), sp.end(), a) > std::count(sp.begin(), sp.end(), b);
            });
            chosen.assign(order.begin(), order.begin() + missing[c]);
        } else {
            std::vector<Option> po;
            for (int x : sp) po.push_back({x, planetName(x), "Exhaust to ignore a prerequisite."});
            chosen = pickMany(p, Tag::SkipPrereq,
                              std::string("Exhaust planet(s) with a ") + colorName(TechColor(c)) + " specialty", po,
                              missing[c], missing[c], "", true);
        }
        // Using a technology specialty exhausts that planet (owner confirmed);
        // Psychoarchaeology lets you use it without exhausting.
        if (!owns(p, "pa")) {
            std::sort(chosen.begin(), chosen.end());
            chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
            for (int x : chosen) G.planets[x].exhausted = true;
            planetsExhausted(p, chosen);  // Arcane Citadel
            if (G.modules & kModDiscordant) dsSpecialtyUsed(p, chosen);  // Ancient Knowledge, Pilgrimage Beacons
        }
    }
    if (resourceCost > 0) resourceCost = sucaban(p, resourceCost);  // Doctor Sucaban (Jol-Nar agent)
    if (resourceCost > 0 && (G.modules & kModThundersEdge)) resourceCost = aello(p, resourceCost);  // Aello (Deepwrought commander)
    if (resourceCost > 0 && !pay(p, resourceCost, false, std::string("researching ") + kTechs[t].name, false)) return false;
    researchTech(p, t);
    E.log(p, pn(p) + " researches " + kTechs[t].name + ".", EventKind::Research);
    if (!usedTears) prophetsTears(p, false);  // or draw 1 action card instead
    if (G.modules & kModDiscordant) dsResearched(p, t);  // Priestess Tuh, Obsessive Designs
    return true;
}

// Specialist Compounds (Jol-Nar breakthrough, Thunder's Edge): exhaust a planet with a technology specialty instead
// of paying the resources, and research a technology of that colour. Returns true when it was used.
bool Rules::specialistCompounds(int p, int cost) {
    if (cost <= 0 || !breakthroughIs(G, p, "jolnar")) return false;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p)) {
        if (G.planets[size_t(x)].exhausted) continue;
        const int sk = planetSkips(G, x);
        for (int c = 0; c < 4; ++c) {
            if (!((sk >> c) & 1)) continue;
            bool any = false;  // something of that colour to research
            for (int t = 0; t < kTechCount && !any; ++t)
                if (kTechs[t].color == TechColor(c) && !kTechs[t].unitUpgrade && canResearchNow(G, p, t) &&
                    (kTechs[t].source == Source::Base || kTechs[t].source == Source::PoK))
                    any = true;
            if (any) opts.push_back({x * 4 + c, planetName(x), std::string("Exhaust it: research a ") + colorName(TechColor(c)) + " technology."});
        }
    }
    if (opts.empty()) return false;
    opts.push_back({-1, "Pay " + std::to_string(cost) + " resources", ""});
    curValue_ = cost;  // (0.44.4: for the AI)
    const int c = fpick(p, Ab::BtJolnar, "Specialist Compounds: exhaust a planet with a technology specialty instead of paying?", opts);
    curValue_ = 0;
    if (c < 0) return false;
    G.planets[size_t(c / 4)].exhausted = true;
    planetsExhausted(p, std::vector<int>{c / 4});
    say(p, pn(p) + " exhausts " + planetName(c / 4) + " (Specialist Compounds).");
    if (G.fixes >= 8) {  // 0.44.4: the Jol-Nar's synergy applies - the paired colour counts as the planet's (The Quantumcore: any)
        int mask = 1 << (c % 4);
        TechColor sa, sb;
        if (allSynergy(G, p)) mask = 15;
        else if (synergyOf(G, p, sa, sb) && (int(sa) == c % 4 || int(sb) == c % 4)) mask |= (1 << int(sa)) | (1 << int(sb));
        onlyColours_ = mask;
        researchStep(p, 0, "Specialist Compounds");
        onlyColours_ = 0;
        return true;
    }
    onlyColour_ = c % 4;
    researchStep(p, 0, "Specialist Compounds");
    onlyColour_ = -1;
    return true;
}

void Rules::technology(int p, bool primary) {
    const bool sciences = lawOwner(G, "minister_sciences") == p;  // Minister of Sciences: no resources needed
    if (!primary) {
        if (!sciences && specialistCompounds(p, 4)) return;
        researchStep(p, sciences ? 0 : 4, sciences ? "Minister of Sciences" : "secondary: 4 resources");
        return;
    }
    if (brikkOk(0)) researchStep(p, 0, "free");
    if (!brikkOk(1)) return;
    bool compounds = false;  // (Specialist Compounds can pay for the second one too)
    if (!sciences && breakthroughIs(G, p, "jolnar"))
        for (int x : controlledPlanets(G, p))
            if (!G.planets[size_t(x)].exhausted && planetSkips(G, x)) compounds = true;
    if (sciences || spendable(G, p, false) >= 6 || compounds)
        if (yesNo(p, Tag::ExtraResearch, sciences ? "Research a second technology (free with Minister of Sciences)?"
                                                  : "Research a second technology for 6 resources?",
                  "Technology primary ability.")) {
            if (!sciences && specialistCompounds(p, 6)) return;
            researchStep(p, sciences ? 0 : 6, sciences ? "Minister of Sciences" : "6 resources");
        }
}

// ------------------------------------------------------------------ 8 Imperial
void Rules::imperial(int p, bool primary) {
    if (!primary) {
        drawSecret(p);
        return;
    }
    std::vector<int> can = scorablePublic(p);
    if (!brikkOk(0)) can.clear();
    if (!can.empty()) {
        std::vector<Option> opts;
        for (int o : can)
            opts.push_back({o, std::string(kObjectives[o].name) + " (" + std::to_string(kObjectives[o].points) + " VP)",
                            objectiveSummary(o)});
        opts.push_back({-1, "Don't score now", ""});
        for (int again = 0; again < 8; ++again) {  // 0.43.0 (owner: Backspace in the payment cost the chance; fixes 2)
            int o = pick(p, Tag::ImperialScore, "Imperial: score a public objective now?", opts);
            if (o < 0 || scoreObjective(p, o) || G.fixes < 2) break;
        }
    }
    if (!brikkOk(1)) return;
    int mec = G.mecatol();
    bool hasMecatol = false;
    if (mec >= 0)
        for (int16_t x : G.systems[mec].planets)
            if (G.planets[x].owner == p) hasMecatol = true;
    if (hasMecatol) {
        gainVP(p, 1, "Imperial: controls Mecatol Rex");
        if (G.modules & kModThundersEdge) custodiaTokens(p);  // Custodian's Favour (Keleres breakthrough)
    }
    else drawSecret(p);
}

}  // namespace ti
