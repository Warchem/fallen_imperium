// Uncharted Space action cards (0.33.0, owner: "add the Uncharted Space action cards ... make bot logic for
// them ... only available when Uncharted Space is active"). The 30 cards are appended to the action card
// table (Source::Uncharted) and go into the deck only with the module. Their timing windows are the same
// as the base game's where one fits; Win::Pass is new. Our own short wording is in card_text.cpp.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool key(int card, const char* k) { return card >= 0 && card < kActionCardCount && !std::strcmp(kActionCards[card].key, k); }
int traitsHeld(const Game& g, int p) {  // bit 0 cultural, 1 hazardous, 2 industrial
    int bits = 0;
    for (int x : controlledPlanets(g, p)) {
        if (hasTraitOf(g, x, Trait::Cultural)) bits |= 1;
        if (hasTraitOf(g, x, Trait::Hazardous)) bits |= 2;
        if (hasTraitOf(g, x, Trait::Industrial)) bits |= 4;
    }
    return bits;
}
}  // namespace

bool usCard(int card) { return card >= 0 && card < kActionCardCount && kActionCards[card].source == Source::Uncharted; }

// ------------------------------------------------------------------ when they can be played
bool Rules::usCardPlayable(int p, int card, const WinCtx& c) const {
    const Player& pl = G.players[p];
    auto is = [&](const char* k) { return key(card, k); };
    switch (c.w) {
        case Win::Pass:
            if (p != c.a) return false;
            if (is("arbitrage")) return true;
            if (is("ai_augury")) return !pl.secrets.empty() && !G.secretDeck.empty();
            return false;
        case Win::AfterActivate:
            // Abyssal Starpaths: the active player joins two systems without planets
            if (is("abyssal_starpaths")) return p == c.active && G.fx.starA < 0 && starpathSystems().size() >= 2;
            // Fulfillment Protocols: any player whose ships are in the activated system (the active player too)
            if (is("fulfillment_protocols_ds")) {
                if (!hasShips(G, p, c.sys)) return false;
                for (UnitType t : {UnitType::Dreadnought, UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer, UnitType::Fighter})
                    if (unitsAvailable(G, p, t) > 0 && unitDef(G, p, t).cost2 <= 8) return true;
            }
            return false;
        case Win::AfterLanding:  // Hostile World: ground forces landing on our planet
            return is("hostile_world_ds") && p == c.b && c.planet >= 0 && planetResources(G, c.planet) > 0;
        case Win::InvasionStart: {  // Echo Shielding: our units in the active system get Planetary Shield
            if (!is("echo_shielding") || p == c.active || (G.fx.echoShield >> p) & 1) return false;
            bool mine = false;
            for (int16_t x : G.systems[c.sys].planets)
                if (G.planets[x].units[p].groundForces() > 0 || G.planets[x].units[p].structures() > 0) mine = true;
            if (!mine) return false;
            for (int t = 0; t < kUnitTypes; ++t)
                if (G.systems[c.sys].space[c.active].n[t] && unitDef(G, c.active, UnitType(t)).bombard > 0) return true;
            return false;
        }
        case Win::CombatStart:
            if (p != c.a && p != c.b) return false;
            if (is("illusory_duplication_ds")) {  // space combat: our non-fighter ships without Sustain Damage
                if (!c.space) return false;
                const Force& f = G.systems[c.sys].space[p];
                for (int t : kShipIdx)
                    if (t != int(UnitType::Fighter) && f.n[t] && !unitSustains(G, p, UnitType(t))) return true;
                return false;
            }
            if (is("freedom_fighters_ds"))  // ground combat: infantry equal to the planet's influence
                return !c.space && c.planet >= 0 && planetInfluence(G, c.planet) > 0 && unitsAvailable(G, p, UnitType::Infantry) > 0;
            return false;
        case Win::CombatRoundStart:
            return is("singularity_charge_ds") && (p == c.a || p == c.b);
        case Win::AgendaStart:
            return is("emergency_meeting") && !(G.agendaDeck.empty() && G.agendaDiscard.empty());
        case Win::TradeAgreement:
            return is("escape_clause") && p == c.a;
        case Win::Research:
            return is("forbidden_knowledge") && p == c.a;
        case Win::StatusStart:
            return is("bounty_contracts") && !G.publicObjectives.empty();
        case Win::GroundHits:
            return is("carapace_plating") && p == c.a && c.value > 0;
        case Win::InfantryKilled:
            return is("contagion") && p == c.a && c.planet >= 0 && G.planets[size_t(c.planet)].units[c.b].count(UnitType::Infantry) > 0;
        case Win::MechKilled: {
            if (!is("safety_overrides_ds") || p != c.a || c.planet < 0) return false;
            return !G.planets[size_t(c.planet)].units[c.b].empty();
        }
        case Win::BeforeAbilityRoll:
            return is("classified_weapons") && p == c.a;
        case Win::AfterDieRolled:
            return is("confused_sage") && sageRoll_ && !sageRoll_->dice.empty();
        case Win::Purged:
            return is("aggressive_broker") && p != c.a;
        case Win::NoteResolved:
            return is("renegotiation") && p == c.a && c.b >= 0 && c.b < G.numPlayers && !G.players[c.b].notes.empty();
        case Win::AfterAgendaRevealed:
            return is("special_session") && !mustVote_;
        default: return false;
    }
}

bool Rules::usComponentUsable(int p, int card) const {
    const Player& pl = G.players[p];
    auto is = [&](const char* k) { return key(card, k); };
    if (is("preparation")) return !G.actionDeck.empty() || !G.actionDiscard.empty();
    if (is("secured_trove")) return fragmentCount(G, p) >= 2 && !G.relicDeck.empty();
    if (is("remnant_collection")) return fragmentCount(G, p) > 0;
    if (is("free_trade_network")) return traitsHeld(G, p) != 0;
    if (is("psionic_hammer")) {
        int best = -1;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[q].eliminated) best = std::max(best, int(G.players[q].vp));
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && G.players[q].vp == best && !G.players[q].secrets.empty()) return true;
        return false;
    }
    if (is("planetary_rigs")) return !rigsPlanets(p).empty();
    if (is("micrometeoroid_storm_ds")) {
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && G.systems[s].space[q].count(UnitType::Fighter) > 0) return true;
        return false;
    }
    if (is("professional_archeologists")) {
        for (int d = 0; d < 4; ++d)
            if (!G.exploreDeck[d].empty() || !G.exploreDiscard[d].empty()) return true;
        return false;
    }
    if (is("personnel_writ")) {
        for (const LeaderState& ls : pl.leaders) {
            const int type = kLeaders[ls.leader].type;
            if (type == kAgent && ls.status == kExhausted) return true;
            if (type == kCommander && ls.status == kLocked && pl.strategy > 0) return true;
        }
        return false;
    }
    return false;
}

// Planetary Rigs: our non-home planets (not Mecatol Rex) with a trait - their deck holds attachments
std::vector<int> Rules::rigsPlanets(int p) const {
    std::vector<int> out;
    for (int x : controlledPlanets(G, p)) {
        if (kPlanets[G.planets[x].def].home || !std::strcmp(kPlanets[G.planets[x].def].key, "mr")) continue;
        if (hasTraitOf(G, x, Trait::Cultural) || hasTraitOf(G, x, Trait::Hazardous) || hasTraitOf(G, x, Trait::Industrial))
            out.push_back(x);
    }
    return out;
}

// ------------------------------------------------------------------ what they do (true = handled here)
bool Rules::usResolve(int p, int card, const WinCtx* c) {
    if (!usCard(card)) return false;
    Player& pl = G.players[p];
    auto is = [&](const char* k) { return key(card, k); };
    const std::string name = kActionCards[card].name;
    const int sys = c ? c->sys : -1;

    if (is("preparation")) {
        drawActionCards(p, 1, name);
        return true;
    }
    if (is("secured_trove")) {
        std::vector<Option> opts;
        for (size_t i = 0; i < pl.fragments.size(); ++i)
            if (isRelicFragment(pl.fragments[i]))
                opts.push_back({int(i), std::string(exploreTypeName(kExploreCards[pl.fragments[i]].type)) + " relic fragment", ""});
        std::vector<int> picks = pickMany(p, Tag::CardTarget, "Secured Trove: purge which 2 relic fragments?", opts, 2, 2);
        std::sort(picks.rbegin(), picks.rend());
        for (int i : picks) pl.fragments.erase(pl.fragments.begin() + i);
        say(p, pn(p) + " purges 2 relic fragments (" + name + ").");
        purged(p);
        gainRelic(p, name);
        return true;
    }
    if (is("remnant_collection")) {
        std::vector<Option> opts;
        for (size_t i = 0; i < pl.fragments.size(); ++i)
            if (isRelicFragment(pl.fragments[i]))
                opts.push_back({int(i), std::string(exploreTypeName(kExploreCards[pl.fragments[i]].type)) + " relic fragment", ""});
        std::vector<int> picks =
            pickMany(p, Tag::CardTarget, "Remnant Collection: purge which relic fragments (1 action card each)?", opts, 1, int(opts.size()));
        std::sort(picks.rbegin(), picks.rend());
        for (int i : picks) pl.fragments.erase(pl.fragments.begin() + i);
        say(p, pn(p) + " purges " + std::to_string(picks.size()) + " relic fragment" + (picks.size() > 1 ? "s" : "") + " (" + name + ").");
        purged(p);
        drawActionCards(p, int(picks.size()), name);
        return true;
    }
    if (is("free_trade_network")) {
        const int bits = traitsHeld(G, p);
        static const char* traitName[3] = {"cultural", "hazardous", "industrial"};
        const int cap = commodityCap(G, pl);
        for (int t = 0; t < 3; ++t) {
            if (!((bits >> t) & 1)) continue;
            std::vector<Option> o;
            if (pl.commodities < cap) o.push_back({1, "Gain 2 commodities", ""});
            if (pl.commodities > 0) o.push_back({2, "Turn 2 commodities into trade goods", ""});
            if (o.empty()) break;
            const int ch = o.size() == 1 ? o[0].id
                                         : pick(p, Tag::CardTarget, std::string("Free Trade Initiative (") + traitName[t] + "): which?", o);
            if (ch == 1) {
                const int n = std::min(2, cap - pl.commodities);
                { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + n); commodityGained(p, cb_); }
                say(p, pn(p) + " gains " + std::to_string(n) + " commodities (" + name + ").");
            } else {
                const int n = std::min(2, int(pl.commodities));
                pl.commodities = int8_t(pl.commodities - n);
                say(p, pn(p) + " turns " + std::to_string(n) + " commodities into trade goods (" + name + ").");
                gainTG(p, n);
            }
        }
        return true;
    }
    if (is("psionic_hammer")) {
        int best = -1;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[q].eliminated) best = std::max(best, int(G.players[q].vp));
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && G.players[q].vp == best && !G.players[q].secrets.empty())
                opts.push_back({q, pn(q), std::to_string(G.players[q].secrets.size()) + " secret objectives"});
        if (opts.empty()) return true;
        const int q = opts.size() == 1 ? opts[0].id : pick(p, Tag::CardTarget, "Neural Hammer: look at whose secret objective?", opts);
        const auto& h = G.players[q].secrets;
        const int o = h[size_t(E.rng().below(uint32_t(h.size())))];
        E.revealed();
        say(p, pn(p) + " looks at one of " + pn(q) + "'s secret objectives (" + name + ").");
        Event e;  // shown to the player who looked (display only)
        e.kind = EventKind::Note;
        e.player = int8_t(p);
        e.text = name + ": " + pn(q) + " holds \"" + kObjectives[o].name + "\" - " + objectiveSummary(o);
        if (G.players[p].human) E.emit(e);
        return true;
    }
    if (is("planetary_rigs")) {
        std::vector<Option> opts;
        for (int x : rigsPlanets(p)) opts.push_back({x, planetName(x), ""});
        const int x = pick(p, Tag::CardTarget, "Planetary Rigs: which planet?", opts);
        int deck = hasTraitOf(G, x, Trait::Cultural) ? 0 : hasTraitOf(G, x, Trait::Hazardous) ? 1 : 2;
        if (hasTraitOf(G, x, Trait::Cultural) + hasTraitOf(G, x, Trait::Hazardous) + hasTraitOf(G, x, Trait::Industrial) > 1) {
            std::vector<Option> d;
            if (hasTraitOf(G, x, Trait::Cultural)) d.push_back({0, "Cultural deck", ""});
            if (hasTraitOf(G, x, Trait::Hazardous)) d.push_back({1, "Hazardous deck", ""});
            if (hasTraitOf(G, x, Trait::Industrial)) d.push_back({2, "Industrial deck", ""});
            deck = pick(p, Tag::CardTarget, "Planetary Rigs: which exploration deck?", d);
        }
        const size_t total = G.exploreDeck[deck].size() + G.exploreDiscard[deck].size();
        for (size_t i = 0; i < total; ++i) {
            const int e = drawExplore(deck);
            if (e < 0) break;
            if (kExploreCards[e].kind == ExploreKind::Attach) {
                say(p, pn(p) + " finds " + kExploreCards[e].name + " for " + planetName(x) + " (" + name + ").", G.planets[x].system);
                resolveExplore(p, e, G.planets[x].system, x);
                return true;
            }
            G.exploreDiscard[deck].push_back(int16_t(e));
        }
        say(p, "No attachment in that deck (" + name + ").");
        return true;
    }
    if (is("micrometeoroid_storm_ds")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            int n = 0;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p) n += G.systems[s].space[q].count(UnitType::Fighter);
            if (n > 0) opts.push_back({int(s), sysName(int(s)), std::to_string(n) + " fighters"});
        }
        const int s = pick(p, Tag::CardTarget, "Micrometeoroid Storm: which system?", opts, "", true);
        for (int q = 0; q < G.numPlayers; ++q) {
            const int n = G.systems[size_t(s)].space[q].count(UnitType::Fighter);
            if (!n) continue;
            RollResult r;
            for (int i = 0; i < n; ++i) {
                const int v = E.rng().d10();
                r.dice.push_back(int8_t(v));
                r.need.push_back(6);
                if (v >= 6) ++r.hits;
            }
            reportRoll(p, s, name + " against " + pn(q) + "'s fighters", r);
            if (r.hits) destroyUnits(q, UnitType::Fighter, r.hits, s, -1, p, "Micrometeoroid Storm");
        }
        return true;
    }
    if (is("professional_archeologists")) {
        const int cap = commodityCap(G, pl);
        for (int d = 0; d < 4; ++d) {
            const int e = drawExplore(d);
            if (e < 0) continue;
            if (kExploreCards[e].kind == ExploreKind::Fragment) {
                pl.fragments.push_back(int16_t(e));
                say(p, pn(p) + " takes a " + std::string(exploreTypeName(kExploreCards[e].type)) + " relic fragment (" + name + ").");
            } else {
                G.exploreDiscard[d].push_back(int16_t(e));
                if (pl.commodities < cap) {
                    { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + 1); commodityGained(p, cb_); }
                    say(p, pn(p) + " discards " + kExploreCards[e].name + " and gains 1 commodity (" + name + ").");
                }
            }
        }
        return true;
    }
    if (is("personnel_writ")) {
        std::vector<Option> opts;
        for (size_t i = 0; i < pl.leaders.size(); ++i) {
            const LeaderState& ls = pl.leaders[i];
            const int type = kLeaders[ls.leader].type;
            if (type == kAgent && ls.status == kExhausted) opts.push_back({int(i), std::string("Ready ") + kLeaders[ls.leader].name, ""});
            if (type == kCommander && ls.status == kLocked && pl.strategy > 0)
                opts.push_back({100 + int(i), std::string("Unlock ") + kLeaders[ls.leader].name, "Spend 1 strategy token."});
        }
        if (opts.empty()) return true;
        const int ch = opts.size() == 1 ? opts[0].id : pick(p, Tag::CardTarget, "Personnel Writ: which?", opts);
        if (ch >= 100) {
            if (G.fixes >= 10) spendStrategy(p, 1);  // (0.44.6: the usual way - Stone's Embrace, Magmus...)
            else pl.strategy = int8_t(pl.strategy - 1);
            pl.leaders[size_t(ch - 100)].status = kReady;
            say(p, pn(p) + " spends a strategy token and unlocks " + kLeaders[pl.leaders[size_t(ch - 100)].leader].name + " (" + name + ").");
        } else {
            readyAgent(p, ch);
            say(p, pn(p) + " readies " + kLeaders[pl.leaders[size_t(ch)].leader].name + " (" + name + ").");
        }
        return true;
    }
    // ---- passing
    if (is("arbitrage")) {
        const int cap = commodityCap(G, pl);
        const int room = std::max(0, cap - pl.commodities);
        const int comm = std::min(4, room), tg = 4 - comm;
        { const int cb_ = pl.commodities; pl.commodities = int8_t(pl.commodities + comm); commodityGained(p, cb_); }
        say(p, pn(p) + " gains " + std::to_string(comm) + " commodities" + (tg ? " and " + std::to_string(tg) + " trade goods" : "") + " (" +
                   name + ").");
        if (tg) gainTG(p, tg);
        return true;
    }
    if (is("ai_augury")) {
        std::vector<Option> opts;
        for (int16_t o : pl.secrets) opts.push_back({o, kObjectives[o].name, objectiveSummary(o)});
        const int o = pick(p, Tag::CardTarget, "AI Augury: return which secret objective?", opts);
        pl.secrets.erase(std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(o)));
        G.secretDeck.push_back(int16_t(o));
        E.rng().shuffle(G.secretDeck);
        say(p, pn(p) + " returns a secret objective (" + name + ").");
        drawSecret(p);
        return true;
    }
    // ---- tactical action
    if (is("fulfillment_protocols_ds")) {
        std::vector<Option> opts;
        for (UnitType t : {UnitType::Dreadnought, UnitType::Carrier, UnitType::Cruiser, UnitType::Destroyer, UnitType::Fighter})
            if (unitsAvailable(G, p, t) > 0 && unitDef(G, p, t).cost2 <= 8) opts.push_back({int(t), unitTypeName(t), ""});
        if (opts.empty()) return true;
        const int t = pick(p, Tag::CardTarget, "Fulfillment Protocols: place which ship?", opts);
        addUnits(p, UnitType(t), 1, sys, -1);
        say(p, pn(p) + " places a " + unitTypeName(UnitType(t)) + " in " + sysName(sys) + " (" + name + ").", sys);
        removeExcessCapacity(p, sys);
        enforceFleetLimit(p);
        return true;
    }
    if (is("hostile_world_ds")) {
        const int lander = c->a, x = c->planet;
        RollResult r;
        for (int i = 0; i < planetResources(G, x); ++i) {
            const int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            r.need.push_back(6);
            if (v >= 6) ++r.hits;
        }
        reportRoll(p, sys, name + " on " + planetName(x), r);
        if (r.hits) assignHits(lander, sys, x, r.hits, (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)), name, p);
        return true;
    }
    if (is("echo_shielding")) {
        G.fx.echoShield = uint8_t(G.fx.echoShield | (1u << p));
        say(p, pn(p) + "'s units in " + sysName(sys) + " gain Planetary Shield (" + name + ").", sys);
        return true;
    }
    // ---- combat
    if (is("illusory_duplication_ds")) {
        G.fx.illusory = uint8_t(G.fx.illusory | (1u << p));
        say(p, pn(p) + "'s ships gain Sustain Damage for this round (" + name + ").", sys);
        return true;
    }
    if (is("freedom_fighters_ds")) {
        const int x = c->planet;
        const int n = std::min(planetInfluence(G, x), unitsAvailable(G, p, UnitType::Infantry));
        addUnits(p, UnitType::Infantry, n, sys, x);
        say(p, pn(p) + " places " + std::to_string(n) + " infantry on " + planetName(x) + " (" + name + ").", sys);
        return true;
    }
    if (is("singularity_charge_ds")) {
        for (int q : {c->a, c->b}) {
            if (q < 0) continue;
            Force& f = c->space ? G.systems[size_t(sys)].space[q] : G.planets[size_t(c->planet)].units[q];
            for (int t = 0; t < kUnitTypes; ++t)
                if (f.n[t] && unitSustainsAt(G, q, UnitType(t), sys, c->space ? -1 : c->planet)) f.dmg[t] = f.n[t];
        }
        say(p, "Every unit in the combat that can be damaged is damaged (" + name + ").", sys);
        return true;
    }
    // ---- agenda phase
    if (is("emergency_meeting")) {
        for (int16_t a : G.agendaDiscard) G.agendaDeck.push_back(a);
        G.agendaDiscard.clear();
        E.rng().shuffle(G.agendaDeck);
        E.revealed();
        std::vector<int16_t> top;
        for (int i = 0; i < 3 && !G.agendaDeck.empty(); ++i) {
            top.push_back(G.agendaDeck.back());
            G.agendaDeck.pop_back();
        }
        std::vector<int16_t> onTop;
        for (int16_t a : top) {
            const int ch = pick(p, Tag::CardTarget, std::string("Emergency Meeting: ") + kAgendas[a].name + " - top or bottom?",
                                {{1, "On top", agendaText(a, false)}, {0, "On the bottom", ""}});
            if (ch == 1) onTop.push_back(a);
            else G.agendaDeck.insert(G.agendaDeck.begin(), a);
        }
        for (auto it = onTop.rbegin(); it != onTop.rend(); ++it) G.agendaDeck.push_back(*it);  // the first chosen comes up first
        say(p, pn(p) + " looks at the next agendas and orders them (" + name + ").");
        return true;
    }
    if (is("special_session")) {
        mustVote_ = true;
        say(p, "Everyone must vote on this agenda (" + name + ").");
        return true;
    }
    // ---- handled by the code that opened the window
    if (is("carapace_plating") || is("escape_clause") || is("forbidden_knowledge") || is("classified_weapons")) return true;
    if (is("contagion")) {
        const int q = c->b, x = c->planet;
        const int n = (G.planets[size_t(x)].units[q].count(UnitType::Infantry) + 1) / 2;
        destroyUnits(q, UnitType::Infantry, n, sys, x, p, "Contagion");
        return true;
    }
    if (is("safety_overrides_ds")) {
        const int q = c->b, x = c->planet;
        std::vector<Option> opts;
        const Force& f = G.planets[size_t(x)].units[q];
        for (int t = 0; t < kUnitTypes; ++t)
            for (int k = 0; k < f.n[t]; ++k) opts.push_back({t * 16 + k, unitTypeName(UnitType(t)), ""});
        const int want = std::min<int>(2, int(opts.size()));
        if (want <= 0) return true;
        std::vector<int> picks = pickMany(q, Tag::CardTarget, "Safety Overrides: destroy which 2 of your units on " + planetName(x) + "?", opts,
                                          want, want);
        for (int id : picks) destroyUnits(q, UnitType(id / 16), 1, sys, x, p, "Safety Overrides");
        return true;
    }
    if (is("confused_sage")) {
        RollResult& r = *sageRoll_;
        std::vector<Option> opts;
        for (size_t i = 0; i < r.dice.size(); ++i)
            opts.push_back({int(i), "Die " + std::to_string(i + 1) + ": " + std::to_string(r.dice[i]),
                            r.dice[i] >= r.need[i] ? "a hit" : "a miss (needs " + std::to_string(r.need[i]) + ")"});
        const int i = pick(p, Tag::CardTarget, "Confused Sage: change which die?", opts,
                           "Confused Sage works on combat rolls and unit ability rolls (space cannon, barrage, bombardment).");
        const int ch = pick(p, Tag::CardTarget, "Confused Sage: add 3 or take 3?", {{1, "+3", ""}, {0, "-3", ""}});
        const bool was = r.dice[size_t(i)] >= r.need[size_t(i)];
        r.dice[size_t(i)] = int8_t(std::max(1, std::min(13, r.dice[size_t(i)] + (ch ? 3 : -3))));
        const bool is2 = r.dice[size_t(i)] >= r.need[size_t(i)];
        if (was != is2) r.hits = std::max(0, r.hits + (is2 ? 1 : -1));
        say(p, pn(p) + " changes a die to " + std::to_string(r.dice[size_t(i)]) + " (" + name + ").", sys);
        return true;
    }
    if (is("aggressive_broker")) {
        const int q = c->a;
        const int room = eventOn(G, kEvCommerce) ? commodityValue(G, pl) : std::max(0, commodityCap(G, pl) - pl.commodities);
        const int ch = pick(q, Tag::CardTarget, "Aggressive Broker: what does " + pn(p) + " get?",
                            {{1, pn(p) + " gains 2 trade goods", ""}, {2, pn(p) + " refills their commodities", std::to_string(room) + " commodities"}});
        if (ch == 1) {
            gainTG(p, 2);
            say(p, pn(p) + " gains 2 trade goods (" + name + ").");
        } else {
            { const int cb_ = pl.commodities; pl.commodities = int8_t(refreshTo(G, pl)); commodityGained(p, cb_); }
            say(p, pn(p) + " refills their commodities (" + name + ").");
        }
        return true;
    }
    if (is("renegotiation")) {
        const int q = c->b;
        std::vector<Option> opts;
        for (int16_t n : G.players[q].notes) opts.push_back({n, noteNameOf(G, n) + " (" + pn(noteOwner(n)) + ")", noteTextOf(G, n)});
        if (opts.empty()) return true;
        const int n = pick(q, Tag::CardTarget, "Renegotiation: give " + pn(p) + " which promissory note?", opts);
        giveNote(q, p, n);
        return true;
    }
    if (is("bounty_contracts")) {
        std::vector<Option> opts;
        for (int16_t o : G.publicObjectives) opts.push_back({o, kObjectives[o].name, objectiveSummary(o)});
        const int o = pick(p, Tag::CardTarget, "Bounty Contracts: which public objective?", opts);
        bounty_.push_back({p, o});
        say(p, pn(p) + " puts a bounty on " + kObjectives[o].name + " (" + name + ").");
        return true;
    }
    if (is("abyssal_starpaths")) {
        std::vector<Option> opts;
        for (int s : starpathSystems()) opts.push_back({s, sysName(s), ""});
        std::vector<int> two = pickMany(p, Tag::CardTarget, "Abyssal Starpaths: join which 2 systems without planets?", opts, 2, 2, "", true);
        G.fx.starA = int16_t(two[0]);
        G.fx.starB = int16_t(two[1]);
        computeAdjacency(G);
        say(p, sysName(two[0]) + " and " + sysName(two[1]) + " are adjacent until the end of this tactical action (" + name + ").");
        return true;
    }
    return true;
}

// Abyssal Starpaths: systems without planets (not hyperlanes)
std::vector<int> Rules::starpathSystems() const {
    std::vector<int> out;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (!G.systems[s].isHyperlane() && G.systems[s].planets.empty() && !G.systems[s].offBoard) out.push_back(int(s));
    return out;
}

// Confused Sage: after any roll, a player holding it may change one die by 3
void Rules::confusedSage(int roller, int other, int sys, RollResult& r) {
    heartOfIxth(roller, other, sys, r);  // Thunder's Edge relic (0.34.0)
    if (!(G.modules & kModUncharted) || r.dice.empty() || roller < 0) return;
    sageRoll_ = &r;
    G.sageDice = r.dice;
    G.sageNeed = r.need;
    G.sageRoller = int8_t(roller);
    G.sageTarget = int8_t(other);
    WinCtx w;
    w.w = Win::AfterDieRolled;
    w.a = roller;
    w.b = other;
    w.sys = sys;
    w.value = r.hits;
    window(w);
    sageRoll_ = nullptr;
    G.sageDice.clear();
    G.sageNeed.clear();
    G.sageRoller = G.sageTarget = -1;
}

// Classified Weapons: before a unit ability roll; +4 and the hits can't be cancelled
int Rules::classifiedWeapons(int p, int sys, int dice) {
    classified_ = false;
    if (!(G.modules & kModUncharted) || p < 0) return 0;
    const int starfall = holdsRelic(p, "starfall_array") ? 1 : 0;  // Starfall Array: +1 to unit ability rolls
    WinCtx w;
    w.w = Win::BeforeAbilityRoll;
    w.a = p;
    w.sys = sys;
    w.value = dice;
    if (!cardHas(window(w), "classified_weapons")) return starfall;
    classified_ = true;
    say(p, pn(p) + " adds 4 to these rolls; their hits can't be cancelled (Classified Weapons).", sys);
    return 4 + starfall;
}

void Rules::purged(int p) {
    // 0.44.4 (fixes 8): every purge reaches Erasure Corps (Lanefir) here, after the purge (the card: "after")
    if (G.fixes >= 8 && p >= 0 && (G.modules & kModDiscordant) && breakthroughsOn(G)) dsbtPurged(p);
    if (!(G.modules & kModUncharted) || p < 0) return;
    WinCtx w;
    w.w = Win::Purged;
    w.a = p;
    window(w);
}

bool Rules::fkReady(int p) const {
    if (!(G.modules & kModUncharted) || !canPlayActionCards(G, p) || ((silenced_ >> p) & 1)) return false;
    for (int16_t c : G.players[p].actionCards)
        if (key(c, "forbidden_knowledge")) return true;
    return false;
}

void Rules::bountyWindow() {
    if (!(G.modules & kModUncharted)) return;
    WinCtx w;
    w.w = Win::StatusStart;
    window(w);
}

// When a player passes (AI Augury, Arbitrage)
void Rules::passWindow(int p) {
    if (!(G.modules & kModUncharted)) return;
    WinCtx c;
    c.w = Win::Pass;
    c.a = p;
    c.active = p;
    window(c);
}

}  // namespace ti
