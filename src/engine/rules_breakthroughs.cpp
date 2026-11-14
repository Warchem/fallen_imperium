// Thunder's Edge breakthroughs (0.34.0): the faction abilities they give (the synergy is in query.cpp). Our own
// wording in breakthroughs.cpp; the AI for their choices in ai/ai_faction.cpp (Ab::Bt*). Each hook does nothing
// unless the player has their breakthrough (breakthroughIs) - so with the module off, or Discordant Stars on, none
// of this runs.
#include <algorithm>
#include <cstring>

#include "engine/breakthroughs.h"
#include "engine/deal.h"
#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
int systemDistance(const Game& g, int a, int b) {  // moves between two systems over adjacency (99: none)
    if (a == b) return 0;
    std::vector<int> d(g.systems.size(), 99), q{a};
    d[size_t(a)] = 0;
    for (size_t i = 0; i < q.size(); ++i)
        for (int16_t n : g.systems[size_t(q[i])].adj)
            if (d[size_t(n)] > d[size_t(q[i])] + 1) {
                d[size_t(n)] = d[size_t(q[i])] + 1;
                if (n == b) return d[size_t(n)];
                q.push_back(n);
            }
    return d[size_t(b)];
}
int planetKey(const char* key) {  // kPlanets index
    for (int i = 0; i < kPlanetCount; ++i)
        if (!std::strcmp(kPlanets[i].key, key)) return i;
    return -1;
}
int supportsHeld(const Game& g, int q) {  // Support for the Throne notes in q's play area
    if (q < 0 || q >= g.numPlayers) return 0;
    int n = 0;
    for (int16_t note : g.players[size_t(q)].notesInPlay)
        if (noteType(note) == kNoteSupportThrone) ++n;
    return n;
}
}  // namespace

// "When you gain this card"
void Rules::btGained(int p) {
    if (G.modules & kModDiscordant) dsbtGained(p);  // Discordant Stars breakthroughs (0.43.0)
    if (fac(p, "muaat") && avernusPlanet() < 0) {  // Stellar Genesis: the Avernus planet next to one of your planets
        const int def = planetKey("avernus");
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size() && def >= 0; ++s) {
            const SystemState& sy = G.systems[s];
            if (sy.isHyperlane() || sy.offBoard || sy.fracture || sy.home >= 0) continue;
            bool near = false;  // a planet we control in it or next to it
            auto mine = [&](int sx) {
                for (int16_t x : G.systems[size_t(sx)].planets)
                    if (G.planets[size_t(x)].owner == p) return true;
                return false;
            };
            near = mine(int(s));
            for (int16_t a : sy.adj) near = near || mine(a);
            if (near) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (!opts.empty()) {
            const int s = fpick(p, Ab::BtMuaat, "Stellar Genesis: place the Avernus planet in which system?", opts,
                                "A non-home system next to a planet you control. You gain it, ready.", true);
            PlanetState ps;
            ps.def = int16_t(def);
            ps.system = int16_t(s);
            G.systems[size_t(s)].planets.push_back(int16_t(G.planets.size()));
            G.planets.push_back(ps);
            const int x = int(G.planets.size()) - 1;
            say(p, "Avernus forms in " + sysName(s) + " (Stellar Genesis).", s);
            setControl(x, p);
            G.planets[size_t(x)].exhausted = false;
        }
    }
    if (fac(p, "yin")) yinAscendant(p);  // Yin Ascendant: a random unused faction's Alliance ability
    if (fac(p, "firmament")) sowingBank(p);  // The Sowing: trade goods onto it
    if (fac(p, "keleres") && !G.players[size_t(p)].custodia) {  // I.I.H.Q. Modernization: the Custodia Vigilia card
        G.players[size_t(p)].custodia = true;
        G.players[size_t(p)].custodiaExhausted = false;
        say(p, pn(p) + " gains the Custodia Vigilia planet card and Custodian's Favour (I.I.H.Q. Modernization).");
    }
    // Al'Raith Ix Ianovar: the Fracture enters play, ingress tokens may move to rifts (0.44.4 fixes 8: the move also when
    // the Fracture was in play already - "after this card enters play")
    if (fac(p, "cabal") && (!G.fractureIn || G.fixes >= 8)) {
        if (!G.fractureIn) fractureEnter(p, true);
        for (int k = 0; k < 2; ++k) {
            std::vector<Option> from, to;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const SystemState& sy = G.systems[s];
                if (sy.ingress && !(sy.d().anomalies & kRift)) from.push_back({int(s), sysName(int(s)), ""});
                if (!sy.ingress && !sy.fracture && (anomaliesFor(G, p, int(s)) & kRift)) to.push_back({int(s), sysName(int(s)), "gravity rift"});
            }
            if (from.empty() || to.empty()) break;
            from.push_back({-1, "Leave them", ""});
            const int a = fpick(p, Ab::BtCabal, "Al'Raith Ix Ianovar: move an ingress token into a gravity rift?", from, "", true);
            if (a < 0) break;
            const int b = fpick(p, Ab::BtCabal, "Move it to which gravity rift?", to, "", true);
            G.systems[size_t(a)].ingress = false;
            G.systems[size_t(b)].ingress = true;
            say(p, "An ingress token moves from " + sysName(a) + " to " + sysName(b) + ".", b);
        }
        computeAdjacency(G);
    }
}

// ------------------------------------------------------------------ component actions
void Rules::btOptions(int p, std::vector<Option>& opts) const {
    // Deepgloom Executable (Yssaril breakthrough): another player may borrow Stall Tactics, if the Yssaril allow it
    // (fixes 27 - found in the Ral Nel review's sims: a person's seat answered by the AI re-asked a refusing Yssaril in a loop)
    // not offered again in the turn the Yssaril said no
    if ((G.modules & kModThundersEdge) && !G.players[size_t(p)].actionCards.empty() && !fac(p, "yssaril") &&
        !(G.fixes >= 27 && stallNoTurn_[p] == G.turnCount))
        for (int y = 0; y < G.numPlayers; ++y)
            if (breakthroughIs(G, y, "yssaril")) {
                opts.push_back({6000 + int(Ab::BtYssaril), "Stall Tactics (the Yssaril's)",
                                "Ask the Yssaril Tribes: if they allow it, discard 1 action card as your action (Deepgloom Executable)."});
                break;
            }
    dsbtOptions(p, opts);  // Discordant Stars breakthroughs (0.43.0)
    // The Nucleus (Avernus's legendary ability): Star Forge without a command token - 0.44.8 (fixes 12, owner: "does
    // Avernus let other factions use Star Forge?"): a legendary planet's card works for whoever controls it, so any
    // controller with a war sun may use the Embers of Muaat's Star Forge through it
    const int av = avernusPlanet();
    auto nucleus = [&](bool anyone) {
        if (av >= 0 && G.planets[size_t(av)].owner == p && !G.planets[size_t(av)].abilityExhausted && (anyone || fac(p, "muaat")) &&
            unitsOnBoard(G, p, UnitType::WarSun) > 0 &&
            (unitsAvailable(G, p, UnitType::Fighter) > 0 || unitsAvailable(G, p, UnitType::Destroyer) > 0))
            opts.push_back({6000 + int(Ab::BtMuaat), "Avernus: The Nucleus", "Exhaust it: Star Forge without spending a command token."});
    };
    if (G.fixes >= 12) nucleus(true);
    if (!hasBreakthrough(G, p)) return;
    const Player& pl = G.players[size_t(p)];
    const BreakthroughDef* bt = breakthroughFor(G, p);
    if (!bt) return;
    if (G.fixes < 12) nucleus(false);
    if (pl.btExhausted) return;
    if (fac(p, "arborec")) {
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].hasToken(p)) {
                bool inf = G.systems[s].space[size_t(p)].count(UnitType::Infantry) > 0;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) inf = true;
                if (inf) {
                    opts.push_back({6000 + int(Ab::BtArborec), std::string("Breakthrough: ") + bt->name, bt->text});
                    break;
                }
            }
    }
    if (fac(p, "mahact") && !G.relicDeck.empty()) {
        for (int16_t t : pl.techs)
            if (kTechs[t].faction < 0 || kTechs[t].faction == pl.faction) {
                opts.push_back({6000 + int(Ab::BtMahact), std::string("Breakthrough: ") + bt->name, bt->text});
                break;
            }
    }
    // (fixes 27, the Naaz-Rokha review) no ACTION on the card - offered in the action list as option 645 (absoluteSynergyReady)
    if (fac(p, "naaz") && !pl.eidolonMax && G.fixes < 27)  // Absolute Synergy: 4 mechs in one system
        for (size_t s = 0; s < G.systems.size(); ++s) {
            int m = G.systems[s].space[size_t(p)].count(UnitType::Mech);
            for (int16_t x : G.systems[s].planets) m += G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech);
            if (m >= 4) {
                opts.push_back({6000 + int(Ab::BtNaaz), std::string("Breakthrough: ") + bt->name, bt->text});
                break;
            }
        }
    if (fac(p, "saar") && spendable(G, p, false) >= 1) {
        bool rocks = false;  // an asteroid field with our ships
        for (size_t s = 0; s < G.systems.size(); ++s)
            if ((G.systems[s].d().anomalies & kAsteroid) && hasShips(G, p, int(s))) rocks = true;
        if (rocks && G.fixes >= 8 && saarTargets(p).empty()) rocks = false;  // 0.44.4: only with a planet to hit
        if (rocks) opts.push_back({6000 + int(Ab::BtSaar), std::string("Breakthrough: ") + bt->name, bt->text});
    }
}

// Deorbit Barrage (Saar): planets with other players' ground forces up to 2 systems from an asteroid field with our ships
std::vector<Option> Rules::saarTargets(int p) const {
    std::vector<Option> opts;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.destroyed) continue;
        bool target = false;
        for (int q = 0; q < kSides; ++q)
            if (q != p && (q < G.numPlayers || q == kNeutral) && ps.units[size_t(q)].groundForces() > 0) target = true;
        if (!target) continue;
        bool inReach = false;  // up to 2 systems from an asteroid field with our ships
        for (size_t s = 0; s < G.systems.size() && !inReach; ++s)
            if ((G.systems[s].d().anomalies & kAsteroid) && hasShips(G, p, int(s)) && systemDistance(G, int(s), ps.system) <= 2)
                inReach = true;
        if (inReach) opts.push_back({int(x), planetName(int(x)), ""});
    }
    return opts;
}

// (fixes 27, the Naaz-Rokha review - owner 2026-11-13) Absolute Synergy: "When you have 4 mechs in the same system, you may
// return 3 of those mechs ... to flip this card" - no ACTION, so a choice in the action list that doesn't use the turn
bool Rules::absoluteSynergyReady(int p) const {
    if (G.fixes < 27 || !hasBreakthrough(G, p) || !fac(p, "naaz")) return false;
    const Player& pl = G.players[size_t(p)];
    if (pl.btExhausted || pl.eidolonMax) return false;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        int m = G.systems[s].space[size_t(p)].count(UnitType::Mech);
        for (int16_t x : G.systems[s].planets) m += G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech);
        if (m >= 4) return true;
    }
    return false;
}

bool Rules::btAction(int p, int ab) {
    Player& pl = G.players[size_t(p)];
    if (ab == int(Ab::BtYssaril)) {  // Deepgloom Executable: borrow Stall Tactics, then perhaps a trade with the Yssaril
        int y = -1;
        for (int q = 0; q < G.numPlayers; ++q)
            if (breakthroughIs(G, q, "yssaril")) y = q;
        if (y < 0 || pl.actionCards.empty()) return false;
        curFor_ = p;
        const bool ok = fyes(y, Ab::BtYssaril, "Deepgloom Executable: let " + pn(p) + " use your Stall Tactics?",
                             "They discard 1 action card as their action; then you may trade with them.");
        curFor_ = -1;
        if (!ok) {
            say(y, pn(y) + " won't share Stall Tactics with " + pn(p) + ".");
            stallNoTurn_[p] = G.turnCount;
            return false;
        }
        std::vector<Option> opts;
        for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
        const int c = fpick(p, Ab::BtYssaril, "Stall Tactics: discard which action card?", opts);
        auto it = std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c));
        if (it != pl.actionCards.end()) {
            pl.actionCards.erase(it);
            discardActionCard(p, c, false);
        }
        say(p, pn(p) + " stalls with the Yssaril's Stall Tactics (Deepgloom Executable).");
        if (G.fixes >= 8) {  // 0.44.4: a transaction with that player, outside the once-per-pair limit (neighbours or not)
            const auto t1 = G.transactions[y][p], t2 = G.transactions[p][y];
            const auto anyway = G.tradeAnyway;
            G.transactions[y][p] = G.transactions[p][y] = 0;
            G.tradeAnyway |= (decltype(G.tradeAnyway)(1) << (y * 8 + p)) | (decltype(G.tradeAnyway)(1) << (p * 8 + y));
            if (transactionAllowed(G, y, p) && canOffer(G, y, p)) negotiate(y, p);
            G.transactions[y][p] = t1;
            G.transactions[p][y] = t2;
            G.tradeAnyway = anyway;
            return true;
        }
        if (transactionAllowed(G, y, p) && canOffer(G, y, p)) negotiate(y, p);
        return true;
    }
    if (ab == int(Ab::BtMuaat)) {  // The Nucleus
        const int av = avernusPlanet();
        if (av < 0) return false;
        std::vector<Option> where;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].space[size_t(p)].count(UnitType::WarSun)) where.push_back({int(s), sysName(int(s)), ""});
        if (where.empty()) return false;
        where.push_back({-1, "Cancel", ""});
        const int s = where.size() == 2 ? where[0].id : fpick(p, Ab::BtMuaat, "The Nucleus: Star Forge in which system?", where, "", true);
        if (s < 0) return false;
        std::vector<Option> what;
        if (unitsAvailable(G, p, UnitType::Fighter) > 0) what.push_back({int(UnitType::Fighter), "2 fighters", ""});
        if (unitsAvailable(G, p, UnitType::Destroyer) > 0) what.push_back({int(UnitType::Destroyer), "1 destroyer", ""});
        what.push_back({-1, "Cancel", ""});
        const int t = fpick(p, Ab::BtMuaat, "The Nucleus: place what?", what, "No command token needed.");
        if (t < 0) return false;
        G.planets[size_t(av)].abilityExhausted = true;
        addUnits(p, UnitType(t), t == int(UnitType::Fighter) ? 2 : 1, s, -1);
        say(p, pn(p) + " forges " + (t == int(UnitType::Fighter) ? "2 fighters" : "a destroyer") + " in " + sysName(s) + " (The Nucleus).", s);
        if (G.fixes >= 12) {  // (0.44.8, owner: it is a use of Star Forge - Ember Colossus, and the usual capacity checks)
            emberColossus(p, s);
            enforceFleetIn(p, s);
            removeExcessCapacity(p, s);
        }
        return true;
    }
    if (ab == int(Ab::BtNaaz)) {  // Absolute Synergy: return 3 of 4 mechs in a system, the card flips
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            int m = G.systems[s].space[size_t(p)].count(UnitType::Mech);
            for (int16_t x : G.systems[s].planets) m += G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech);
            if (m >= 4) opts.push_back({int(s), sysName(int(s)), std::to_string(m) + " mechs"});
        }
        if (opts.empty()) return false;
        opts.push_back({-1, "Cancel", ""});
        const int s = fpick(p, Ab::BtNaaz, "Absolute Synergy: return 3 mechs from which system?", opts, "", true);
        if (s < 0) return false;
        int left = 3;  // from the space area first, then the planets with the most
        removing_ = true;
        const int sp = std::min(left, int(G.systems[size_t(s)].space[size_t(p)].count(UnitType::Mech)));
        if (sp > 0) {
            G.systems[size_t(s)].space[size_t(p)].remove(UnitType::Mech, sp, false);
            left -= sp;
        }
        for (int16_t x : G.systems[size_t(s)].planets) {
            const int k = std::min(left, int(G.planets[size_t(x)].units[size_t(p)].count(UnitType::Mech)));
            if (k > 0) {
                G.planets[size_t(x)].units[size_t(p)].remove(UnitType::Mech, k, false);
                left -= k;
            }
        }
        removing_ = false;
        pl.eidolonMax = true;
        pl.btExhausted = true;  // (the card is on the mech card now)
        say(p, pn(p) + " returns 3 mechs: Eidolon Maximum rises in " + sysName(s) + " (Absolute Synergy).", s);
        return true;
    }
    if (ab == int(Ab::BtArborec)) {  // Psychospore
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (!G.systems[s].hasToken(p)) continue;
            // (0.44.4 fixes 8: infantry in the space area count too - the action list already offered it for them)
            bool inf = G.fixes >= 8 && G.systems[s].space[size_t(p)].count(UnitType::Infantry) > 0;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) inf = true;
            if (inf) opts.push_back({int(s), sysName(int(s)), ""});
        }
        if (opts.empty()) return false;
        opts.push_back({-1, "Cancel", ""});
        const int s = fpick(p, Ab::BtArborec, "Psychospore: take back your command token from which system?", opts, "", true);
        if (s < 0) return false;
        pl.btExhausted = true;
        G.systems[size_t(s)].tokens &= uint16_t(~(1u << p));
        int at = -1;
        for (int16_t x : G.systems[size_t(s)].planets)
            if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::Infantry)) at = x;
        if (at < 0 && G.fixes >= 8)  // (infantry only in space: a planet we control there, else the space area)
            for (int16_t x : G.systems[size_t(s)].planets)
                if (G.planets[size_t(x)].owner == p) at = x;
        if (at >= 0 && unitsAvailable(G, p, UnitType::Infantry) > 0) addUnits(p, UnitType::Infantry, 1, s, at);
        else if (at < 0 && G.fixes >= 8 && unitsAvailable(G, p, UnitType::Infantry) > 0) {
            addUnits(p, UnitType::Infantry, 1, s, -1);
            removeExcessCapacity(p, s);
        }
        say(p, pn(p) + " takes back the command token in " + sysName(s) + " and places 1 infantry (Psychospore).", s);
        return true;
    }
    if (ab == int(Ab::BtMahact)) {  // Vaults of the Heir
        std::vector<Option> opts;
        for (int16_t t : pl.techs)
            if (kTechs[t].faction < 0 || kTechs[t].faction == pl.faction) opts.push_back({t, std::string("Purge ") + kTechs[t].name, ""});
        if (opts.empty() || G.relicDeck.empty()) return false;
        opts.push_back({-1, "Cancel", ""});
        const int t = fpick(p, Ab::BtMahact, "Vaults of the Heir: purge which technology for a relic?", opts);
        if (t < 0) return false;
        pl.btExhausted = true;
        pl.techs.erase(std::find(pl.techs.begin(), pl.techs.end(), int16_t(t)));
        if (kTechs[t].unitUpgrade)  // the unit goes back to its earlier version
            for (int u = 0; u < kUnitCount; ++u)
                if (kUnits[u].tech == t && pl.unit[int(kUnits[u].type)] == u && kUnits[u].upgradeOf >= 0)
                    pl.unit[int(kUnits[u].type)] = kUnits[u].upgradeOf;
        say(p, pn(p) + " purges " + kTechs[t].name + " (Vaults of the Heir).");
        if (G.fixes >= 8) purged(p);  // (0.44.4: Erasure Corps, Aggressive Broker)
        gainRelic(p, "Vaults of the Heir");
        return true;
    }
    if (ab == int(Ab::BtSaar)) {  // Deorbit Barrage
        std::vector<Option> opts = saarTargets(p);
        if (opts.empty()) return false;
        opts.push_back({-1, "Cancel", ""});
        const int x = fpick(p, Ab::BtSaar, "Deorbit Barrage: bombard which planet?", opts, "", false, true);
        if (x < 0) return false;
        const int most = spendable(G, p, false);
        std::vector<Option> amt;
        // ((fixes 27, the Saar review - owner 2026-11-13) "spend any amount of resources": no 10-dice cap)
        const int cap = G.fixes >= 27 ? most : std::min(most, 10);
        for (int k = 1; k <= cap; ++k) amt.push_back({k, std::to_string(k) + " dice", std::to_string(k) + " resources"});
        curValue_ = x;  // (0.44.4: the planet, for the AI)
        const int k = fpick(p, Ab::BtSaar, "Deorbit Barrage: spend how many resources (1 die each)?", amt);
        curValue_ = 0;
        if (!pay(p, k, false, "Deorbit Barrage", false)) return false;
        pl.btExhausted = true;
        const PlanetState& ps = G.planets[size_t(x)];
        int victim = -1;
        for (int q = 0; q < kSides; ++q)
            if (q != p && (q < G.numPlayers || q == kNeutral) && ps.units[size_t(q)].groundForces() > 0) victim = q;
        RollResult r;
        for (int i = 0; i < k; ++i) {
            const int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            r.need.push_back(4);
            r.unit.push_back(-1);
            r.bonus9.push_back(0);
        }
        recount(r);
        reportRoll(p, ps.system, "Deorbit Barrage on " + planetName(x), r);
        if (victim >= 0 && r.hits > 0)
            assignHits(victim, ps.system, x, r.hits, (1 << int(UnitType::Infantry)) | (1 << int(UnitType::Mech)), "Deorbit Barrage", p);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ hooks
void Rules::btControl(int planet, int p) {  // Fealty Uplink (L1Z1X): infantry equal to the planet's influence
    if (G.modules & kModDiscordant) dsbtControl(planet, p);  // Rhodun's Reliquary (0.43.0)
    if (!breakthroughIs(G, p, "l1z1x")) return;
    const int k = std::min(planetInfluence(G, planet), unitsAvailable(G, p, UnitType::Infantry));
    if (k <= 0 || isStation(G, planet)) return;
    addUnits(p, UnitType::Infantry, k, G.planets[size_t(planet)].system, planet);
    say(p, pn(p) + " places " + std::to_string(k) + " infantry on " + planetName(planet) + " (Fealty Uplink).", G.planets[size_t(planet)].system);
}

void Rules::btWonCombat(int winner, int loser, int sys) {  // N'orr Supremacy (Sardakk): a command token or a unit upgrade
    (void)loser;
    if (!breakthroughIs(G, winner, "sardakk")) return;
    if (G.fixes >= 5) {
        // 0.43.4 (tester): the research goes through the normal research step (unit upgrades only), so specialty
        // planets, AI Development Algorithm and the other ways to ignore a prerequisite count
        onlyUpgradeTypes_ = 0xFFFF;
        researchDry_ = true;
        const bool can = !fac(winner, "nekro") && researchStep(winner, 0, "N'orr Supremacy");
        researchDry_ = false;
        onlyUpgradeTypes_ = 0;
        std::vector<Option> opts{{0, "Gain 1 command token", ""}};
        if (can) opts.push_back({1, "Research a unit upgrade", "Prerequisites as usual: specialty planets, AI Development Algorithm..."});
        const int c = opts.size() == 1 ? 0 : fpick(winner, Ab::BtSardakk, "N'orr Supremacy: gain 1 command token or research a unit upgrade?", opts);
        bool researched = false;
        if (c == 1) {
            say(winner, pn(winner) + " researches a unit upgrade (N'orr Supremacy).", sys);
            onlyUpgradeTypes_ = 0xFFFF;
            researched = researchStep(winner, 0, "N'orr Supremacy");
            onlyUpgradeTypes_ = 0;
        }
        if (!researched) {
            say(winner, pn(winner) + " gains 1 command token (N'orr Supremacy).", sys);
            gainTokens(winner, 1, false, "N'orr Supremacy: gain 1 command token.");
        }
        (void)sys;
        return;
    }
    std::vector<Option> opts{{0, "Gain 1 command token", ""}};
    for (int t = 0; t < kTechCount; ++t) {
        if (!kTechs[t].unitUpgrade || !techAvailableToResearch(G, winner, t)) continue;
        int missing[4];
        missingPrereqs(G, winner, t, missing);
        if (missing[0] + missing[1] + missing[2] + missing[3] == 0) opts.push_back({1000 + t, std::string("Research ") + kTechs[t].name, ""});
    }
    const int c = fpick(winner, Ab::BtSardakk, "N'orr Supremacy: gain 1 command token or research a unit upgrade?", opts);
    if (c >= 1000) {
        researchTech(winner, c - 1000);
        E.log(winner, pn(winner) + " researches " + kTechs[c - 1000].name + " (N'orr Supremacy).", EventKind::Research);
    } else {
        gainTokens(winner, 1, false, "N'orr Supremacy: gain 1 command token.");
    }
    (void)sys;
}

void Rules::btProduced(int p, int sys, const int counts[kUnitTypes]) {  // Auto-Factories (Hacan)
    (void)sys;
    if (!breakthroughIs(G, p, "hacan")) return;
    int ships = 0;
    for (int t : kShipIdx)
        if (t != int(UnitType::Fighter)) ships += counts[t];
    if (ships < 3 || reinforcementTokens(p) <= 0) return;
    G.players[size_t(p)].fleet = int8_t(G.players[size_t(p)].fleet + 1);
    say(p, pn(p) + " puts 1 command token in the fleet pool (Auto-Factories).");
}

void Rules::btTurnStart(int p) {
    if ((G.modules & kModDiscordant) && breakthroughsOn(G) && p == G.activePlayer) dsbtTurnStart(p);  // Arms Brokerage (0.43.0)  // Thunder's Paradox (Nomad): exhaust one of your agents to ready another agent
    if (!breakthroughIs(G, p, "nomad")) return;
    if (G.fixes >= 8 && nomadQuiet_[p] == G.round) return;  // (0.44.4: "don't ask again this round")
    Player& pl = G.players[size_t(p)];
    std::vector<int> ready, tired;
    for (size_t i = 0; i < pl.leaders.size(); ++i) {
        if (kLeaders[pl.leaders[i].leader].type != kAgent) continue;
        if (pl.leaders[i].status == kReady) ready.push_back(int(i));
        if (pl.leaders[i].status == kExhausted) tired.push_back(int(i));
    }
    // 0.44.4 (fixes 8): "ready ANY other agent" - other players' exhausted agents too (id 10000 + q * 256 + a * 16 + b)
    std::vector<std::pair<int, int>> others;
    if (G.fixes >= 8)
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || G.players[size_t(q)].eliminated) continue;
            const auto& ls = G.players[size_t(q)].leaders;
            for (size_t i = 0; i < ls.size(); ++i)
                if (kLeaders[ls[i].leader].type == kAgent && ls[i].status == kExhausted) others.push_back({q, int(i)});
        }
    if (ready.empty() || (tired.empty() && others.empty())) return;
    std::vector<Option> opts;
    for (int a : ready)
        for (int b : tired)
            opts.push_back({a * 16 + b, std::string("Exhaust ") + kLeaders[pl.leaders[size_t(a)].leader].name + ", ready " +
                                            kLeaders[pl.leaders[size_t(b)].leader].name,
                            ""});
    for (int a : ready)
        for (const auto& o : others)
            opts.push_back({10000 + o.first * 256 + a * 16 + o.second,
                            std::string("Exhaust ") + kLeaders[pl.leaders[size_t(a)].leader].name + ", ready " + pn(o.first) + "'s " +
                                kLeaders[G.players[size_t(o.first)].leaders[size_t(o.second)].leader].name,
                            ""});
    opts.push_back({-1, "Not now", ""});
    if (G.fixes >= 8) opts.push_back({-2, "Not now - don't ask again this round", ""});
    const int c = fpick(p, Ab::BtNomad, "Thunder's Paradox: swap which agents?", opts);
    if (c == -2) nomadQuiet_[p] = int16_t(G.round);
    if (c < 0) return;
    // (0.44.5, fixes 9: exhausted the usual way - Temporal Command Suite may ready it again)
    auto exhaustNth = [&](int idx) {
        if (G.fixes < 9) {
            pl.leaders[size_t(idx)].status = kExhausted;
            return;
        }
        int nth = 0;
        for (int i = 0; i < idx; ++i)
            if (kLeaders[pl.leaders[size_t(i)].leader].type == kAgent) ++nth;
        exhaustAgent(p, nth);
    };
    if (c >= 10000) {
        const int q = (c - 10000) / 256, a = ((c - 10000) % 256) / 16, b = (c - 10000) % 16;
        exhaustNth(a);
        readyAgent(q, b);
        say(p, pn(p) + " exhausts one of their agents to ready " + pn(q) + "'s " + kLeaders[G.players[size_t(q)].leaders[size_t(b)].leader].name +
                   " (Thunder's Paradox).");
        return;
    }
    exhaustNth(c / 16);
    readyAgent(p, c % 16);
    say(p, pn(p) + " exhausts one agent to ready another (Thunder's Paradox).");
}

int Rules::btCombatBonus(int p, int opp, int sys, bool space) {  // Imperator (Winnu): +1 per Support for the Throne they hold
    (void)sys;
    (void)space;
    if (!breakthroughIs(G, p, "winnu")) return 0;
    return supportsHeld(G, opp);
}

void Rules::btActivated(int p, int sys) {  // Imperator (Winnu): a legendary planet's system - one ship moves 1 further
    if (breakthroughIs(G, p, "empyrean") && G.fixes < 26) voidTether(p, sys);  // (fixes 26: "When you activate" - earlier)
    if (G.fixes < 26) wingTransfer(p, sys);  // (fixes 26: "When you activate" - right after the activation, rules_tactical)
    if (!breakthroughIs(G, p, "winnu")) return;
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (isLegendaryPlanet(G, x)) {
            G.fx.imperator = true;
            return;
        }
}

// Wing Transfer (Argent breakthrough): when you activate a system that contains only your units - command tokens into
// adjacent systems that contain only your units; the move comes at the end of the action (wingTransferEnd)
void Rules::wingTransfer(int p, int sys) {
    if (!breakthroughIs(G, p, "argent")) return;
    auto onlyMine = [&](int s) {
        if (!hasUnits(G, p, s)) return false;
        for (int q = 0; q < kSides; ++q)
            if (q != p && hasUnits(G, q, s)) return false;
        // (fixes 26, the Argent review - owner 2026-11-13) a planet another player controls with none of their units on it
        // holds no units of theirs
        if (G.fixes < 26)
            for (int16_t x : G.systems[size_t(s)].planets)
                if (G.planets[size_t(x)].owner >= 0 && G.planets[size_t(x)].owner != p) return false;
        return true;
    };
    if (onlyMine(sys)) {
        if (G.fixes >= 8) G.fx.wing.push_back(-1);  // (0.44.4: the card triggered - the move at the end of the action is open)
        std::vector<Option> opts;
        for (int16_t a : G.systems[size_t(sys)].adj)
            if (onlyMine(a) && !G.systems[size_t(a)].hasToken(p)) opts.push_back({a, sysName(a), ""});
        const int most = std::min(int(opts.size()), reinforcementTokens(p));
        if (most > 0) {
            std::vector<int> picks = fpickMany(p, Ab::BtArgent, "Wing Transfer: place command tokens in which adjacent systems?", opts, 0,
                                               most, "From your reinforcements. At the end of this action, ships may move among them and the "
                                                         "active system.");
            for (int a : picks) {
                G.systems[size_t(a)].tokens = uint16_t(G.systems[size_t(a)].tokens | (1u << p));
                G.fx.wing.push_back(int16_t(a));
            }
            if (!picks.empty()) say(p, pn(p) + " places " + std::to_string(picks.size()) + " command token(s) next to " + sysName(sys) + " (Wing Transfer).", sys);
        }
    }
}

int Rules::btLetnevBonus(int p, int sys) {  // Gravleash Maneuvers: X = the ship types p has in the combat
    if (!breakthroughIs(G, p, "letnev")) return 0;
    int types = 0;
    for (int t : kShipIdx)
        if (G.systems[size_t(sys)].space[size_t(p)].n[t]) ++types;
    return types;
}

// ------------------------------------------------------------------ part 3: the rest
int Rules::avernusPlanet() const {
    if (!(G.modules & kModThundersEdge)) return -1;
    for (size_t x = 0; x < G.planets.size(); ++x)
        if (!G.planets[x].destroyed && !std::strcmp(kPlanets[G.planets[x].def].key, "avernus")) return int(x);
    return -1;
}

// Stellar Genesis: after a war sun moves out of Avernus's system into a non-home system, Avernus may go along.
// (A war sun passing through it isn't noticed - RULES-CHECK.)
void Rules::avernusCarry(int p, int sys, int avSys, int wsBefore) {
    const int av = avernusPlanet();
    if (av < 0 || avSys < 0 || avSys == sys || !breakthroughIs(G, p, "muaat") || G.systems[size_t(sys)].home >= 0) return;
    if (G.systems[size_t(avSys)].space[size_t(p)].count(UnitType::WarSun) >= wsBefore && !(G.fixes >= 26 && avernusThrough_))
        return;  // no war sun left it (or, fixes 26, went through it)
    if (!G.systems[size_t(sys)].space[size_t(p)].count(UnitType::WarSun)) return;
    if (!fyes(p, Ab::BtMuaat, "Stellar Genesis: move Avernus with your war sun to " + sysName(sys) + "?", "The planet and its units go along.")) return;
    auto& from = G.systems[size_t(avSys)].planets;
    from.erase(std::remove(from.begin(), from.end(), int16_t(av)), from.end());
    G.systems[size_t(sys)].planets.push_back(int16_t(av));
    G.planets[size_t(av)].system = int16_t(sys);
    say(p, "Avernus follows " + pn(p) + "'s war sun to " + sysName(sys) + " (Stellar Genesis).", sys);
    // (0.45.0, fixes 14 - owner's bug: a space dock on Avernus held fighters there - without it they lack capacity)
    if (G.fixes >= 14) {
        for (int q = 0; q < G.numPlayers; ++q) removeExcessCapacity(q, avSys);
    }
}

// Yin Ascendant: the Alliance (commander) ability of a random faction nobody plays (commanderUsable reads it).
void Rules::yinAscendant(int p) {
    if (!breakthroughIs(G, p, "yin")) return;
    Player& pl = G.players[size_t(p)];
    std::vector<int> pool;
    // (fixes 25, owner's faction debug pass - Nocturne) the Firmament / the Obsidian are one faction (TE rulebook, Minor
    // Factions): one entry, unused only while neither is played; the side is picked at random
    const int firm = findFaction("firmament"), obs = findFaction("obsidian");
    const bool one = G.fixes >= 25;
    for (int f = 0; f < kFactionCount; ++f) {
        if (!factionImplemented(f) || !std::strcmp(kFactions[f].key, "keleres") || !std::strcmp(kFactions[f].key, "yin")) continue;
        if (kFactions[f].source == Source::Discordant && !(G.modules & kModDiscordant)) continue;  // (Discordant Stars: with the module)
        if (one && f == obs) continue;
        const bool pair = one && f == firm;
        bool used = std::find(pl.yinAlliances.begin(), pl.yinAlliances.end(), int8_t(f)) != pl.yinAlliances.end() ||
                    (pair && std::find(pl.yinAlliances.begin(), pl.yinAlliances.end(), int8_t(obs)) != pl.yinAlliances.end());
        for (int q = 0; q < G.numPlayers; ++q)
            if (G.players[size_t(q)].faction == f || (pair && G.players[size_t(q)].faction == obs) ||
                (isFaction(G, q, "keleres") && G.players[size_t(q)].tribunii == f))
                used = true;
        if (!used) pool.push_back(f);
    }
    if (pool.empty()) return;
    int f = pool[size_t(E.rng().below(int(pool.size())))];
    if (one && f == firm && obs >= 0 && E.rng().below(2)) f = obs;  // (the Obsidian side)
    E.revealed();
    pl.yinAlliances.push_back(int8_t(f));
    say(p, pn(p) + " gains the Alliance ability of the " + kFactions[f].name + " (Yin Ascendant).");
}

// Slumberstate Computing (0.44.9, fixes 13 - the owner's answer B9): other players may allow the Titans to place a
// sleeper token on a planet they control - asked of that player (any time on the Titans' turn, no transaction needed)
std::vector<Option> Rules::sleeperAskSpots(int p) const {
    std::vector<Option> v;
    if (G.fixes < 13 || !(G.modules & kModThundersEdge) || !breakthroughsOn(G) || !breakthroughIs(G, p, "titans") || !fac(p, "titans")) return v;
    int n = 0;
    for (const PlanetState& ps : G.planets) n += ps.sleeper ? 1 : 0;
    if (n >= 5) return v;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const PlanetState& ps = G.planets[x];
        if (ps.sleeper || ps.destroyed || ps.owner < 0 || ps.owner == p || ps.owner >= G.numPlayers || isHomePlanet(G, int(x))) continue;
        if (G.players[size_t(ps.owner)].eliminated || ((sleeperAsked_ >> ps.owner) & 1)) continue;
        v.push_back({int(x), planetName(int(x)) + " (" + pn(ps.owner) + ")", ""});
    }
    return v;
}
bool Rules::sleeperAsk(int p) {
    std::vector<Option> opts = sleeperAskSpots(p);
    if (opts.empty()) return false;
    opts.push_back({-1, "Cancel", ""});
    const int x = fpick(p, Ab::BtTitans, "Slumberstate Computing: ask to place a sleeper token on which planet?", opts,
                        "Its owner may allow it (asked once per player each turn).", false, true);
    if (x < 0) return false;
    const int q = G.planets[size_t(x)].owner;
    sleeperAsked_ = uint16_t(sleeperAsked_ | (1u << q));
    const int cf = curFor_, cv = curValue_;
    curFor_ = p;
    curValue_ = -x - 1;  // (for the AI: which planet - negative, so it isn't read as the coexistence question's)
    const bool ok = fyes(q, Ab::BtTitans, pn(p) + " asks to place a sleeper token on your " + planetName(x) + " - allow it?",
                         "Slumberstate Computing. Awaken may later turn it into a Titans PDS there.");
    curFor_ = cf;
    curValue_ = cv;
    if (!ok) {
        say(p, pn(q) + " won't have a sleeper on " + planetName(x) + ".", G.planets[size_t(x)].system);
        return true;
    }
    G.planets[size_t(x)].sleeper = true;
    say(p, pn(q) + " lets " + pn(p) + " place a sleeper token on " + planetName(x) + " (Slumberstate Computing).", G.planets[size_t(x)].system);
    return true;
}

// Slumberstate Computing: Coalescence would start a ground combat - the Titans' units may coexist instead.
bool Rules::slumberstate(int p, int planet) {
    if (!breakthroughIs(G, p, "titans")) return false;
    curValue_ = planet;  // (0.44.4: for the AI)
    const bool coexist = fyes(p, Ab::BtTitans, "Slumberstate Computing: coexist on " + planetName(planet) + " instead of fighting?",
                              "Your awakened units share the planet; in each status phase you and that player draw 1 more action card.");
    curValue_ = 0;
    if (!coexist) return false;
    G.planets[size_t(planet)].coexist = uint16_t(G.planets[size_t(planet)].coexist | (1u << p));
    say(p, pn(p) + "'s units coexist on " + planetName(planet) + " (Slumberstate Computing).", G.planets[size_t(planet)].system);
    return true;
}

void Rules::btStatusDraws() {
    for (int p = 0; p < G.numPlayers; ++p) {
        if (!breakthroughIs(G, p, "titans")) continue;
        uint16_t with = 0;  // players the Titans coexist with
        for (const PlanetState& ps : G.planets) {
            if (ps.destroyed || ps.units[size_t(p)].empty()) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !ps.units[size_t(q)].empty() && (((ps.coexist >> p) & 1) || ((ps.coexist >> q) & 1))) with = uint16_t(with | (1u << q));
        }
        for (int q = 0; q < G.numPlayers; ++q)
            if ((with >> q) & 1) {
                drawActionCards(p, 1, "Slumberstate Computing");
                drawActionCards(q, 1, "Slumberstate Computing");
            }
    }
}

// Wing Transfer: at the end of the action, ships may move among the active system and the systems given a token.
void Rules::wingTransferEnd(int p, int sys) {
    if (G.fx.wing.empty() || !breakthroughIs(G, p, "argent")) return;
    std::vector<int> zone{sys};
    if (G.fixes >= 8) {  // 0.44.4: the active system and every adjacent system with our command token (not other players' ships)
        for (int16_t a : G.systems[size_t(sys)].adj) {
            if (!G.systems[size_t(a)].hasToken(p)) continue;
            bool others = false;
            for (int q = 0; q < kSides; ++q)
                if (q != p && G.systems[size_t(a)].space[size_t(q)].nonFighterShips() + G.systems[size_t(a)].space[size_t(q)].count(UnitType::Fighter) > 0)
                    others = true;
            if (!others) zone.push_back(a);
        }
        if (zone.size() < 2) return;
    } else {
        for (int16_t a : G.fx.wing) zone.push_back(a);
    }
    for (int step = 0; step < 6; ++step) {
        std::vector<Option> opts;  // id = from * 100000 + to * 16 + unit type
        for (int a : zone)
            for (int t : kShipIdx) {
                if (!G.systems[size_t(a)].space[size_t(p)].n[t]) continue;
                for (int b : zone)
                    if (b != a)
                        opts.push_back({a * 100000 + b * 16 + t, std::string("1 ") + unitTypeName(UnitType(t)) + ": " + sysName(a) + " -> " + sysName(b), ""});
            }
        if (opts.empty()) return;
        opts.push_back({-1, "Done", ""});
        const int c = fpick(p, Ab::BtArgent, "Wing Transfer: move a ship?", opts,
                            G.fixes >= 8 ? "A ship with capacity takes along what it can of what would otherwise be left without room." : "");
        if (c < 0) break;
        const int a = c / 100000, b = (c % 100000) / 16, t = c % 16;
        Force& src = G.systems[size_t(a)].space[size_t(p)];
        const bool dmg = src.dmg[t] > 0 && src.dmg[t] >= src.n[t];
        src.remove(UnitType(t), 1, dmg);
        G.systems[size_t(b)].space[size_t(p)].add(UnitType(t), 1);
        if (dmg) G.systems[size_t(b)].space[size_t(p)].dmg[t] = uint8_t(G.systems[size_t(b)].space[size_t(p)].dmg[t] + 1);
        say(p, pn(p) + " moves a " + unitTypeName(UnitType(t)) + " from " + sysName(a) + " to " + sysName(b) + " (Wing Transfer).", b);
        if (G.fixes >= 8) {  // 0.44.4: its cargo goes with it (what the system left behind has no room for), up to its capacity
            int room = unitDef(G, p, UnitType(t)).capacity;
            for (UnitType u : {UnitType::Infantry, UnitType::Mech, UnitType::Fighter}) {
                while (room > 0 && capacityOverflow(G, p, a) > 0 && G.systems[size_t(a)].space[size_t(p)].count(u) > 0) {
                    G.systems[size_t(a)].space[size_t(p)].remove(u, 1, false);
                    G.systems[size_t(b)].space[size_t(p)].add(u, 1);
                    --room;
                }
            }
            removeExcessCapacity(p, b);
        }
        removeExcessCapacity(p, a);
    }
    if (G.fixes >= 8) enforceFleetLimit(p);  // (0.44.4: the systems the ships went to count too)
}

// Void Tether: after activating a system with or next to our units or planets - place (or move) a tether on one of
// its borders. Two tokens.
void Rules::voidTether(int p, int sys) {
    bool near = hasUnits(G, p, sys);
    for (int16_t x : G.systems[size_t(sys)].planets) near = near || G.planets[size_t(x)].owner == p;
    for (int16_t a : G.systems[size_t(sys)].adj) {
        near = near || hasUnits(G, p, a);
        for (int16_t x : G.systems[size_t(a)].planets) near = near || G.planets[size_t(x)].owner == p;
    }
    if (!near) return;
    std::vector<Option> opts;
    for (int16_t a : G.systems[size_t(sys)].adj) {
        // 0.44.4 (fixes 8): a border the two systems share on the board - not a wormhole, hyperlane or ingress link
        if (G.fixes >= 8 && (hexDistance(G.systems[size_t(sys)].pos, G.systems[size_t(a)].pos) != 1 || G.systems[size_t(sys)].offBoard ||
                             G.systems[size_t(a)].offBoard || G.systems[size_t(sys)].isHyperlane() || G.systems[size_t(a)].isHyperlane()))
            continue;
        bool already = false;
        for (const auto& t : G.tethers)
            if ((t.first == sys && t.second == a) || (t.first == a && t.second == sys)) already = true;
        if (!already) opts.push_back({a, "Between " + sysName(sys) + " and " + sysName(a), ""});
    }
    if (opts.empty()) return;
    opts.push_back({-1, "No tether", ""});
    const int a = fpick(p, Ab::BtEmpyrean, std::string("Void Tether: ") + (G.tethers.size() >= 2 ? "move" : "place") + " a tether on a border of " +
                                               sysName(sys) + "?",
                        opts, "Other players don't treat the two systems as adjacent.", true);
    if (a < 0) return;
    // (fixes 26, the Empyrean review - owner 2026-11-12: the card) "place or move 1 of your Void Tether tokens" - the
    // Empyrean choose which (before: the older one moved once both were out)
    int moved = G.tethers.size() >= 2 ? 0 : -1;
    if (G.fixes >= 26 && !G.tethers.empty()) {
        std::vector<Option> w;
        if (G.tethers.size() < 2) w.push_back({999, "Place a new tether", "You have 2 Void Tether tokens; 1 is on the board."});
        for (size_t i = 0; i < G.tethers.size(); ++i)
            w.push_back({1000 + int(i), "Move the tether between " + sysName(G.tethers[i].first) + " and " + sysName(G.tethers[i].second), ""});
        const int c = fpick(p, Ab::BtEmpyrean, "Void Tether: place a new tether or move one?", w,
                            "To the border between " + sysName(sys) + " and " + sysName(a) + ".");
        moved = c >= 1000 ? c - 1000 : -1;
    }
    if (moved >= 0 && moved < int(G.tethers.size())) G.tethers.erase(G.tethers.begin() + moved);
    G.tethers.push_back({int16_t(sys), int16_t(a)});
    say(p, pn(p) + " tethers the border between " + sysName(sys) + " and " + sysName(a) + " (Void Tether).", sys);
}

// (fixes 26, the Empyrean review - owner 2026-11-12: the card) Void Tether: "other players do not treat those systems as
// adjacent to each other unless you allow it" - after another player activates a system, a person playing the Empyrean is
// asked when a tether stops one of that player's ships from getting there; bots never allow it (as before).
void Rules::voidTetherAllow(int p, int sys) {
    if (G.tethers.empty() || p < 0 || p >= G.numPlayers || ((G.fx.tetherAllow >> p) & 1)) return;
    for (int e = 0; e < G.numPlayers; ++e) {
        if (e == p || !breakthroughIs(G, e, "empyrean") || !G.players[size_t(e)].human || G.players[size_t(e)].eliminated ||
            tetherQuiet_[e] == G.round)
            continue;
        bool matters = false;
        const int bonus = moveBonusMax(G, p);
        for (size_t f = 0; f < G.systems.size() && !matters; ++f) {
            if (int(f) == sys || G.systems[f].hasToken(p)) continue;
            for (int t : kShipIdx) {
                if (matters || !G.systems[f].space[p].n[t] || shipReach(G, p, int(f), sys, UnitType(t), bonus).ok) continue;
                G.fx.tetherAllow = uint8_t(G.fx.tetherAllow | (1u << p));
                if (shipReach(G, p, int(f), sys, UnitType(t), bonus).ok) matters = true;
                G.fx.tetherAllow = uint8_t(G.fx.tetherAllow & ~(1u << p));
            }
        }
        if (!matters) return;
        const int c = fpick(e, Ab::BtEmpyrean, "Void Tether: let " + pn(p) + " treat your tethered borders as adjacent this tactical action?",
                            {{1, "Yes", ""}, {0, "No", ""}, {3, "No, and don't ask again this round", "You'll be asked again next round."}},
                            pn(p) + " activated " + sysName(sys) + "; a tether stops some of their ships from getting there.");
        if (c == 3) tetherQuiet_[e] = int16_t(G.round);
        if (c != 1) return;
        G.fx.tetherAllow = uint8_t(G.fx.tetherAllow | (1u << p));
        say(e, pn(e) + " lets " + pn(p) + " cross the Void Tethers.", sys);
        return;
    }
}

// (the same) space cannon offense: q's deep space cannon would fire into sys across a tether - a person playing the
// Empyrean may allow it; bots never do.
void Rules::tetherCannonAllow(int q, int p, int sys) {
    if (G.tethers.empty() || q < 0 || q >= G.numPlayers || ((G.fx.tetherAllow >> q) & 1)) return;
    for (int e = 0; e < G.numPlayers; ++e) {
        if (e == q || !breakthroughIs(G, e, "empyrean") || !G.players[size_t(e)].human || G.players[size_t(e)].eliminated ||
            tetherQuiet_[e] == G.round)
            continue;
        const int before = cannonGuns(q, p, sys).guns.total();
        G.fx.tetherAllow = uint8_t(G.fx.tetherAllow | (1u << q));
        const int after = cannonGuns(q, p, sys).guns.total();
        G.fx.tetherAllow = uint8_t(G.fx.tetherAllow & ~(1u << q));
        if (after <= before) return;
        const int c = fpick(e, Ab::BtEmpyrean, "Void Tether: let " + pn(q) + "'s space cannon fire across your tether into " + sysName(sys) + "?",
                            {{1, "Yes", ""}, {0, "No", ""}, {3, "No, and don't ask again this round", "You'll be asked again next round."}},
                            std::to_string(after - before) + " of " + pn(q) + "'s units would fire from the other side of a tethered border.");
        if (c == 3) tetherQuiet_[e] = int16_t(G.round);
        if (c != 1) return;
        G.fx.tetherAllow = uint8_t(G.fx.tetherAllow | (1u << q));
        say(e, pn(e) + " lets " + pn(q) + "'s space cannon fire across the Void Tether.", sys);
        return;
    }
}

// Custodian's Favour: when another player scores Imperial's Mecatol point, the Keleres gain 2 command tokens.
void Rules::custodiaTokens(int scorer) {
    for (int k = 0; k < G.numPlayers; ++k)
        if (k != scorer && breakthroughIs(G, k, "keleres") && G.players[size_t(k)].custodia)
            gainTokens(k, 2, false, "Custodian's Favour: gain 2 command tokens.");
}

void Rules::eidolonLost(int p) {
    Player& pl = G.players[size_t(p)];
    if (!pl.eidolonMax) return;
    pl.eidolonMax = false;
    pl.btExhausted = false;
    say(p, "Eidolon Maximum falls: the card flips back (Absolute Synergy).");
}

}  // namespace ti
