// Thunder's Edge (0.34.0, owner: "Thunder's Edge implementation"), a module like Uncharted Space: its tiles,
// planets and rules only with kModThundersEdge. Part 1: the new kinds of planets - two traits / two
// specialties (query.cpp, rules_explore), space stations (here and query.cpp), the entropic scar
// (query.cpp scarAt and the unit ability hooks) - and the legendary planets in the tile pools.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

// ------------------------------------------------------------------ space stations
void Rules::stationControl(int sys) {
    if (sys < 0 || sys >= int(G.systems.size())) return;
    int only = -1, n = 0;
    for (int q = 0; q < G.numPlayers; ++q)
        if (hasShips(G, q, sys)) {
            only = q;
            ++n;
        }
    if (n != 1) return;
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (isStation(G, x) && G.planets[size_t(x)].owner != only && !G.planets[size_t(x)].destroyed) setControl(x, only);
}

bool Rules::stationConvert(int p) {
    Player& pl = G.players[size_t(p)];
    if (pl.commodities <= 0) return false;
    for (int x : controlledPlanets(G, p))
        if (stationFor(G, p, x) && !G.planets[size_t(x)].exhausted) {
            G.planets[size_t(x)].exhausted = true;
            const int n = pl.commodities;
            pl.commodities = 0;
            gainTG(p, n);
            say(p, pn(p) + " exhausts " + planetName(x) + ": " + std::to_string(n) + " commodit" + (n == 1 ? "y" : "ies") +
                       " become trade goods.");
            return true;
        }
    // (0.44.9) an Omni-Forgeworld: its station card would be exhausted - the war sun takes damage instead
    if (forgeworldsOf(G, p, true) > 0)
        for (size_t s = 0; s < G.systems.size(); ++s) {
            Force& f = G.systems[s].space[size_t(p)];
            if (f.count(UnitType::WarSun) <= f.dmg[int(UnitType::WarSun)]) continue;
            f.dmg[int(UnitType::WarSun)] = uint8_t(f.dmg[int(UnitType::WarSun)] + 1);
            const int n = pl.commodities;
            pl.commodities = 0;
            gainTG(p, n);
            say(p, pn(p) + "'s Omni-Forgeworld in " + sysName(int(s)) + " takes damage instead of exhausting its station: " + std::to_string(n) +
                       " commodit" + (n == 1 ? "y" : "ies") + " become trade goods.",
                int(s));
            return true;
        }
    return false;
}

// ------------------------------------------------------------------ legendary planets (Thunder's Edge)
namespace {
int ownedPlanet(const Game& g, int p, const char* key) {
    for (size_t x = 0; x < g.planets.size(); ++x)
        if (g.planets[x].owner == p && !g.planets[x].destroyed && !std::strcmp(kPlanets[g.planets[x].def].key, key)) return int(x);
    return -1;
}
}  // namespace

std::vector<int16_t> Rules::salvageOf(int p) const {
    std::vector<int16_t> v;
    for (const PlanetState& ps : G.planets)
        if (ps.owner == p && !ps.salvage.empty()) v.insert(v.end(), ps.salvage.begin(), ps.salvage.end());
    return v;
}

bool Rules::takeSalvage(int p, int card) {
    for (PlanetState& ps : G.planets) {
        if (ps.owner != p) continue;
        auto it = std::find(ps.salvage.begin(), ps.salvage.end(), int16_t(card));
        if (it == ps.salvage.end()) continue;
        ps.salvage.erase(it);
        return true;
    }
    return false;
}

// End of the turn: The Acropolis (Emelpar) - ready another component that isn't a strategy card;
// The Galactic Council (Mecatol Rex, with the module) - discard a secret objective to draw another.
void Rules::teEndOfTurn(int p) {
    if (!(G.modules & kModThundersEdge) || G.players[size_t(p)].eliminated) return;
    Player& pl = G.players[size_t(p)];
    const int acro = ownedPlanet(G, p, "emelpar");
    if (acro >= 0 && !G.planets[size_t(acro)].abilityExhausted) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p))
            if (G.planets[size_t(x)].exhausted)
                opts.push_back({x, "Ready " + planetName(x), std::to_string(planetResources(G, x)) + " res / " + std::to_string(planetInfluence(G, x)) + " inf"});
        for (int16_t t : pl.exhaustedTechs) opts.push_back({10000 + t, std::string("Ready ") + kTechs[t].name, "technology"});
        for (int16_t r : pl.exhaustedRelics) opts.push_back({20000 + r, std::string("Ready ") + kRelics[r].name, "relic"});
        for (size_t i = 0; i < pl.leaders.size(); ++i)
            if (kLeaders[pl.leaders[i].leader].type == kAgent && pl.leaders[i].status == kExhausted)
                opts.push_back({30000 + int(i), std::string("Ready ") + kLeaders[pl.leaders[i].leader].name, "agent"});
        for (int x : controlledPlanets(G, p))
            if (x != acro && G.planets[size_t(x)].abilityExhausted && *legendaryName(G.planets[size_t(x)].def))
                opts.push_back({40000 + x, std::string("Ready ") + legendaryName(G.planets[size_t(x)].def), "legendary planet ability"});
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int c = pick(p, Tag::LegendaryAbility, "The Acropolis (Emelpar): ready which component?", opts, legendaryText(G.planets[size_t(acro)].def));
            if (c >= 0) {
                G.planets[size_t(acro)].abilityExhausted = true;
                std::string what;
                if (c >= 40000) {
                    G.planets[size_t(c - 40000)].abilityExhausted = false;
                    what = legendaryName(G.planets[size_t(c - 40000)].def);
                } else if (c >= 30000) {
                    readyAgent(p, c - 30000);
                    what = kLeaders[pl.leaders[size_t(c - 30000)].leader].name;
                } else if (c >= 20000) {
                    pl.exhaustedRelics.erase(std::remove(pl.exhaustedRelics.begin(), pl.exhaustedRelics.end(), int16_t(c - 20000)), pl.exhaustedRelics.end());
                    what = kRelics[c - 20000].name;
                } else if (c >= 10000) {
                    pl.exhaustedTechs.erase(std::remove(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(c - 10000)), pl.exhaustedTechs.end());
                    what = kTechs[c - 10000].name;
                } else {
                    G.planets[size_t(c)].exhausted = false;
                    what = planetName(c);
                }
                say(p, pn(p) + " readies " + what + " (The Acropolis).");
            }
        }
    }
    const int mr = ownedPlanet(G, p, "mr");
    if (mr >= 0 && !G.planets[size_t(mr)].abilityExhausted && !pl.secrets.empty() && !G.secretDeck.empty()) {
        std::vector<Option> opts;
        for (int16_t o : pl.secrets) opts.push_back({o, std::string("Discard ") + kObjectives[o].name, objectiveSummary(o)});
        opts.push_back({-1, "Not now", ""});
        const int o = pick(p, Tag::LegendaryAbility, "The Galactic Council (Mecatol Rex): discard a secret objective to draw another?", opts,
                           "Thunder's Edge: Mecatol Rex is a legendary planet.");
        if (o >= 0) {
            G.planets[size_t(mr)].abilityExhausted = true;
            pl.secrets.erase(std::find(pl.secrets.begin(), pl.secrets.end(), int16_t(o)));
            G.secretDeck.push_back(int16_t(o));
            E.rng().shuffle(G.secretDeck);
            say(p, pn(p) + " discards a secret objective (The Galactic Council).");
            drawSecret(p);
        }
    }
    // Thunder's Edge (the planet): exhaust it to take another action
    const int te = ownedPlanet(G, p, "thundersedge");
    if (te >= 0 && !G.planets[size_t(te)].abilityExhausted && !pl.passed &&
        pick(p, Tag::LegendaryAbility, "Thunder's Edge: exhaust it to take another action?", {{1, "Yes", ""}, {0, "No", ""}},
             legendaryText(G.planets[size_t(te)].def)) == 1) {
        G.planets[size_t(te)].abilityExhausted = true;
        say(p, pn(p) + " takes another action (Thunder's Edge).");
        takeTurn(p, true, true);
    }
    teExpedition(p);  // part 3: commit to the expedition
}

// When the player passes: Maxis Central Control (Faunus), Dok 'N Pic's Salvage Yard (Garbozia), Aeurex Mechanica (Industrex)
void Rules::tePass(int p) {
    if (!(G.modules & kModThundersEdge) || G.players[size_t(p)].eliminated) return;
    const int faunus = ownedPlanet(G, p, "faunus");
    if (faunus >= 0 && !G.planets[size_t(faunus)].abilityExhausted) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            const PlanetState& ps = G.planets[x];
            if (ps.destroyed || ps.owner == p || isHomePlanet(G, int(x)) || isLegendaryPlanet(G, int(x)) || !ps.attachments.empty()) continue;
            if (ps.system < 0 || isStation(G, int(x))) continue;
            bool units = false;
            for (int q = 0; q < G.numPlayers; ++q)
                if (!ps.units[q].empty()) units = true;
            if (G.fixes >= 22 && !ps.units[size_t(kNeutral)].empty()) units = true;  // (0.58.5: neutral units - the Fracture, minor factions)
            if (units) continue;
            opts.push_back({int(x), planetName(int(x)),
                            (ps.owner >= 0 ? pn(ps.owner) + "'s - " : std::string("unclaimed - ")) + std::to_string(planetResources(G, int(x))) +
                                " res / " + std::to_string(planetInfluence(G, int(x))) + " inf"});
        }
        if (!opts.empty()) {
            opts.push_back({-1, "Not now", ""});
            const int x = pick(p, Tag::LegendaryAbility, "Maxis Central Control (Faunus): gain control of which planet?", opts,
                               legendaryText(G.planets[size_t(faunus)].def), false, true);
            if (x >= 0) {
                G.planets[size_t(faunus)].abilityExhausted = true;
                say(p, pn(p) + " takes " + planetName(x) + " without a shot (Maxis Central Control).", G.planets[size_t(x)].system);
                setControl(x, p);
            }
        }
    }
    // Hyperion VI (Ordinian, the Last Bastion's home): 1 action card and 1 command token
    const int ordinian = ownedPlanet(G, p, "ordinian");
    if (ordinian >= 0 && !G.planets[size_t(ordinian)].abilityExhausted &&
        yesNo(p, Tag::LegendaryAbility, "Hyperion VI (Ordinian): draw 1 action card and gain 1 command token?", legendaryText(G.planets[size_t(ordinian)].def))) {
        G.planets[size_t(ordinian)].abilityExhausted = true;
        drawActionCards(p, 1, "Hyperion VI");
        gainTokens(p, 1, false, "Hyperion VI: gain 1 command token.");
    }
    const int garb = ownedPlanet(G, p, "garbozia");
    if (garb >= 0 && !G.planets[size_t(garb)].abilityExhausted && !G.actionDiscard.empty()) {
        std::vector<Option> opts;
        for (int16_t c : G.actionDiscard) {
            bool dup = false;
            for (const Option& o : opts) dup = dup || o.id == c;
            if (!dup) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
        }
        opts.push_back({-1, "Not now", ""});
        const int c = pick(p, Tag::LegendaryAbility, "Dok 'N Pic's Salvage Yard (Garbozia): put which action card on it?", opts,
                           legendaryText(G.planets[size_t(garb)].def));
        if (c >= 0) {
            G.planets[size_t(garb)].abilityExhausted = true;
            G.actionDiscard.erase(std::find(G.actionDiscard.begin(), G.actionDiscard.end(), int16_t(c)));
            G.planets[size_t(garb)].salvage.push_back(int16_t(c));
            say(p, pn(p) + " puts " + kActionCards[c].name + " on Dok 'N Pic's Salvage Yard (it can be played from there).");
        }
    }
    const int indu = ownedPlanet(G, p, "industrex");
    if (indu >= 0 && !G.planets[size_t(indu)].abilityExhausted) {
        // a ship matching one of our unit upgrade technologies, into a system with our ships
        std::vector<Option> kinds;
        for (int t : kShipIdx) {
            if (UnitType(t) == UnitType::Fighter && false) continue;
            const UnitDef& u = unitDef(G, p, UnitType(t));
            if (u.tech < 0 || !G.players[size_t(p)].hasTech(u.tech) || unitsAvailable(G, p, UnitType(t)) <= 0) continue;
            kinds.push_back({t, u.name, std::to_string(u.cost2 / 2) + " cost"});
        }
        std::vector<Option> where;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (hasShips(G, p, int(s))) where.push_back({int(s), sysName(int(s)), ""});
        if (!kinds.empty() && !where.empty()) {
            kinds.push_back({-1, "Not now", ""});
            const int t = pick(p, Tag::LegendaryAbility, "Aeurex Mechanica (Industrex): place which ship?", kinds, legendaryText(G.planets[size_t(indu)].def));
            if (t >= 0) {
                const int s = pick(p, Tag::LegendaryAbility, "Aeurex Mechanica: place the " + std::string(unitTypeName(UnitType(t))) + " where?", where, "", true);
                G.planets[size_t(indu)].abilityExhausted = true;
                addUnits(p, UnitType(t), 1, s, -1);
                say(p, pn(p) + " places a " + unitTypeName(UnitType(t)) + " in " + sysName(s) + " (Aeurex Mechanica).", s);
                removeExcessCapacity(p, s);
                enforceFleetLimit(p);
            }
        }
    }
}

// After activating a system: the Ionian Fuel Refinery (Tempesta) - 1 ship moves 1 further this tactical action.
void Rules::tempestaWindow(int p, int sys) {
    if (!(G.modules & kModThundersEdge)) return;
    const int tp = ownedPlanet(G, p, "tempesta");
    if (tp < 0 || G.planets[size_t(tp)].abilityExhausted) return;
    // only worth asking when a ship is exactly one move short
    bool short1 = false;
    for (size_t s = 0; s < G.systems.size() && !short1; ++s) {
        if (int(s) == sys || G.systems[s].hasToken(p)) continue;
        for (int t : kShipIdx)
            if (!short1 && G.systems[s].space[size_t(p)].n[t] && UnitType(t) != UnitType::Fighter && !shipReach(G, p, int(s), sys, UnitType(t), moveBoost(G, p) ? 1 : 0).ok &&
                shipReach(G, p, int(s), sys, UnitType(t), moveBoost(G, p) ? 2 : 1).ok)
                short1 = true;
    }
    if (!short1) return;
    if (!yesNo(p, Tag::LegendaryAbility, "Ionian Fuel Refinery (Tempesta): 1 of your ships moves 1 further this tactical action?",
               legendaryText(G.planets[size_t(tp)].def)))
        return;
    G.planets[size_t(tp)].abilityExhausted = true;
    G.fx.tempesta = true;
    say(p, pn(p) + " fuels a ship at the Ionian Fuel Refinery: it can move 1 further.", sys);
}

// Entropic scars (Thunder's Edge rulebook, 0.43.4 - the tester found it missing; fixes >= 5): at the start of the
// status phase a player with ships in an entropic scar may spend a token from their strategy pool to gain one of
// their faction technologies.
// (fixes 27 - the owner 2026-11-14) the Scepter of Emelpar ("When you would spend a token from your strategy pool, you may exhaust
// this card to spend a token from your reinforcements instead") pays for it too; and a person with ships in a scar but nothing to
// spend it with is told so in the feed (a note: not in the game log)
void Rules::scarStatusStart() {
    const bool v27 = G.fixes >= 27;
    for (int p : initiativeOrder(G)) {
        Player& pl = G.players[size_t(p)];
        const bool emel = v27 && !pl.eliminated && relicReady(p, "emelpar") && reinforcementTokens(p) > 0;
        if (pl.eliminated || (pl.strategy <= 0 && !emel && !(pl.human && v27))) continue;
        int where = -1;
        for (size_t s = 0; s < G.systems.size() && where < 0; ++s)
            if (scarAt(G, int(s)) && hasShips(G, p, int(s))) where = int(s);
        if (where < 0) continue;
        std::vector<Option> opts;
        for (int t = 0; t < kTechCount; ++t)
            if (kTechs[t].faction >= 0 && kTechs[t].faction == pl.faction && !pl.hasTech(t) && techImplemented(t))
                opts.push_back({t, kTechs[t].name, techSummary(t)});
        if (opts.empty()) continue;
        if (pl.strategy <= 0 && !emel) {  // (a person only - see above)
            feedNote(p, "Your ships are in the entropic scar at " + sysName(where) +
                            ", but your strategy pool is empty: no faction technology this status phase (it costs 1 strategy token).");
            continue;
        }
        const bool onlyScepter = pl.strategy <= 0;
        opts.push_back({-1, "Nothing", onlyScepter ? "Keep the Scepter of Emelpar ready." : "Keep the strategy token."});
        const int t = fpick(p, Ab::ScarTech,
                            "Entropic scar (" + sysName(where) + "): spend a strategy token to gain one of your faction technologies?", opts,
                            onlyScepter ? "Your strategy pool is empty: the Scepter of Emelpar is exhausted to spend a token from your reinforcements instead." : "");
        if (t < 0 || t >= kTechCount || (pl.strategy <= 0 && !emel)) continue;
        bool scepterUsed = false;
        if (v27 && emel) {
            if (onlyScepter) {
                exhaustRelic(p, "emelpar");
                scepterUsed = true;
            } else {
                scepterUsed = scepter(p);
            }
        }
        if (scepterUsed) {
            // (a token from the reinforcements: spent tokens go back there, so nothing moves)
        } else if (G.fixes >= 10) {
            spendStrategy(p, 1);  // (0.44.6: the usual way - Stone's Embrace, the Rook, Magmus...)
        } else {
            --pl.strategy;
        }
        researchTech(p, t, false);
        say(p, pn(p) + (scepterUsed ? " exhausts the Scepter of Emelpar in the entropic scar at " : " spends a strategy token in the entropic scar at ") +
                   sysName(where) + " and gains " + kTechs[t].name + ".", where);
    }
}

}  // namespace ti
