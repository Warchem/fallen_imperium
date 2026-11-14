// Action cards: drawing and the hand limit, timing windows (who may play
// what, in which order), Sabotage and Reverse Engineer, every card's effect,
// component actions (action cards and technologies), start / end of turn
// technologies.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool isKey(int card, const char* k) { return card >= 0 && card < kActionCardCount && !std::strcmp(kActionCards[card].key, k); }

bool participant(const WinCtx& c, int p) { return p == c.a || p == c.b; }

const char* winText(Win w) {
    switch (w) {
        case Win::StrategyStart: return "The strategy phase is starting.";
        case Win::PickStrategy: return "A strategy card was just chosen.";
        case Win::AfterActivate: return "A system was just activated.";
        case Win::StrategicAction: return "A strategic action is about to be performed.";
        case Win::AfterAction: return "You just finished an action.";
        case Win::AfterMove: return "Ships just moved into the active system.";
        case Win::BeforeCannonHits: return "Space cannon hits are about to be assigned to your ships.";
        case Win::AfterUnitRoll: return "Another player just made a bombardment, space cannon or anti-fighter barrage roll.";
        case Win::CombatStart: return "A combat is starting.";
        case Win::CombatRoundStart: return "A combat round is starting.";
        case Win::CombatRoundEnd: return "A combat round just ended.";
        case Win::BeforeAFB: return "You are about to roll anti-fighter barrage.";
        case Win::AnnounceRetreats: return "Retreats are about to be announced.";
        case Win::AfterRetreat: return "Your opponent just announced a retreat.";
        case Win::BeforeSpaceHits: return "You are about to assign hits to your ships.";
        case Win::AfterSustain: return "A ship just used Sustain Damage.";
        case Win::ShipDestroyed: return "One of your ships was just destroyed.";
        case Win::WonSpaceCombat: return "You won the space combat.";
        case Win::InvasionStart: return "An invasion is starting.";
        case Win::AfterLanding: return "Ground forces are landing on your planet.";
        case Win::AfterGroundRoll: return "You just rolled for ground combat.";
        case Win::GainControl: return "You are gaining control of a planet.";
        case Win::LoseControl: return "You just lost control of a planet.";
        case Win::Production: return "Your units are about to produce.";
        case Win::ReturnStrategyCards: return "Strategy cards are about to be returned.";
        case Win::AgendaStart: return "The agenda phase is starting.";
        case Win::AgendaRevealed: return "An agenda was just revealed.";
        case Win::AfterAgendaRevealed: return "An agenda was revealed - riders and similar cards can be played now.";
        case Win::AfterSpeakerVotes: return "The speaker has voted.";
        case Win::AfterCastVotes: return "You just cast your votes.";
        case Win::Elected: return "A player is being elected.";
        case Win::OutcomeResolve: return "The agenda outcome is about to be resolved.";
        case Win::Pass: return "You are passing.";
        default: return "";
    }
}

int costOf(const Game& g, int p, UnitType t) {
    int c2 = unitDef(g, p, t).cost2;
    return c2 <= 0 ? 0 : (c2 + 1) / 2;
}
}  // namespace

// ============================================================ deck and hand
void Rules::drawActionCards(int p, int n, const std::string& why) {
    if (n <= 0 || G.players[p].eliminated) return;
    if (G.modules & kModDiscordant) {  // Autonetic Memory (Cymiae): a card from the discard pile or an infantry instead
        n = dsAutonetic(p, n);
        if (n <= 0) return;
    }
    int drawn = 0;
    for (int i = 0; i < n; ++i) {
        if (G.actionDeck.empty()) {
            if (G.actionDiscard.empty()) break;
            G.actionDeck = G.actionDiscard;  // LRR: shuffle the discard pile to make a new deck
            G.actionDiscard.clear();
            E.rng().shuffle(G.actionDeck);
            say(-1, "The action card deck is reshuffled.");
        }
        G.players[p].actionCards.push_back(G.actionDeck.back());
        G.actionDeck.pop_back();
        ++drawn;
    }
    if (!drawn) return;
    E.revealed();
    for (int i = 0; i < drawn; ++i) {  // 0.29.0 (owner): your new cards open large (display only, not in the log)
        Event e;
        e.kind = EventKind::CardDrawn;
        e.player = int8_t(p);
        e.count = G.players[p].actionCards[G.players[p].actionCards.size() - size_t(drawn) + size_t(i)];
        E.emit(std::move(e));
    }
    say(p, pn(p) + " draws " + std::to_string(drawn) + " action card" + (drawn > 1 ? "s" : "") +
               (why.empty() ? "" : " (" + why + ")") + ".");
    // Scheming (Yssaril): draw 1 more, then discard 1 from the hand
    if (fac(p, "yssaril") && why != "Scheming") {
        Player& pl = G.players[p];
        int before = int(pl.actionCards.size());
        drawActionCards(p, 1, "Scheming");
        if (int(pl.actionCards.size()) > before || !pl.actionCards.empty()) {
            std::vector<Option> opts;
            for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
            const int prev = curAbility_;
            curAbility_ = int(Ab::Scheming);
            int c = pick(p, Tag::DiscardActionCard, "Scheming: discard 1 action card", opts, abilityText(pl.faction, 1));
            curAbility_ = prev;
            pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
            discardActionCard(p, c, false);
            pl.usedFactionAbility = true;
            say(p, pn(p) + " discards 1 action card (Scheming).");
        }
    }
    if (why != "Scheming") sharedScheming(p);
    enforceHandLimit(p);
}

// Deepgloom Executable (Yssaril breakthrough; 0.44.9, fixes 13 - the owner's answer B9): the Yssaril may let another
// player use Scheming when they draw action cards (1 more, then discard 1) - and may then trade with them outside the
// once-per-pair limit, as with the shared Stall Tactics. The drawer asks; the Yssaril confirm. People can say "not
// again this round"; a bot asks the Yssaril at most once a round.
void Rules::sharedScheming(int p) {
    if (G.fixes < 13 || !(G.modules & kModThundersEdge) || !breakthroughsOn(G) || p < 0 || p >= G.numPlayers || fac(p, "yssaril")) return;
    int y = -1;
    for (int q = 0; q < G.numPlayers; ++q)
        if (q != p && breakthroughIs(G, q, "yssaril") && fac(q, "yssaril") && !G.players[size_t(q)].eliminated) y = q;
    if (y < 0 || G.players[size_t(p)].eliminated || G.actionDeck.size() + G.actionDiscard.size() == 0) return;
    Player& pl = G.players[size_t(p)];
    if (pl.human) {
        if (schemeQuiet_[p] == G.round) return;
        const int c = fpick(p, Ab::BtYssaril, "Ask the Yssaril to let you use Scheming (draw 1 more, then discard 1)?",
                           {{1, "Yes, ask them", ""}, {0, "No", ""}, {3, "No, and don't ask again this round", "You'll be asked again next round."}},
                           "Deepgloom Executable (Yssaril breakthrough).");
        if (c == 3) schemeQuiet_[p] = int16_t(G.round);
        if (c != 1) return;
    } else {
        if (schemeAsked_[p] == G.round) return;
        schemeAsked_[p] = int16_t(G.round);
    }
    curFor_ = p;
    bool ok;
    if (G.players[size_t(y)].human) {
        if (schemeQuiet_[y] == G.round) ok = false;
        else {
            const int c = fpick(y, Ab::BtYssaril, "Deepgloom Executable: let " + pn(p) + " use your Scheming?",
                               {{1, "Yes", ""}, {0, "No", ""}, {3, "No - and refuse everyone this round", ""}},
                               "They draw 1 more action card, then discard 1; then you may trade with them.");
            if (c == 3) schemeQuiet_[y] = int16_t(G.round);
            ok = c == 1;
        }
    } else {
        ok = fyes(y, Ab::BtYssaril, "Deepgloom Executable: let " + pn(p) + " use your Scheming?",
                  "They draw 1 more action card, then discard 1; then you may trade with them.");
    }
    curFor_ = -1;
    if (!ok) return;
    drawActionCards(p, 1, "Scheming");
    if (!pl.actionCards.empty()) {
        std::vector<Option> opts;
        for (int16_t c : pl.actionCards) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
        const int prev = curAbility_;
        curAbility_ = int(Ab::Scheming);
        const int c = pick(p, Tag::DiscardActionCard, "Scheming (the Yssaril's): discard 1 action card", opts);
        curAbility_ = prev;
        pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(c)));
        discardActionCard(p, c, false);
    }
    say(p, pn(p) + " uses the Yssaril's Scheming (Deepgloom Executable).");
    // the trade, outside the once-per-pair limit - on one of their turns, not in the middle of a combat
    if (G.phase == Phase::Action && (p == G.activePlayer || y == G.activePlayer) && !inSpaceCombat_ && !inGroundCombat_) {
        const auto t1 = G.transactions[y][p], t2 = G.transactions[p][y];
        const auto anyway = G.tradeAnyway;
        G.transactions[y][p] = G.transactions[p][y] = 0;
        G.tradeAnyway |= (decltype(G.tradeAnyway)(1) << (y * 8 + p)) | (decltype(G.tradeAnyway)(1) << (p * 8 + y));
        if (transactionAllowed(G, y, p) && canOffer(G, y, p)) negotiate(y, p);
        G.transactions[y][p] = t1;
        G.transactions[p][y] = t2;
        G.tradeAnyway = anyway;
    }
}

void Rules::enforceHandLimit(int p) {
    Player& pl = G.players[p];
    const int limit = actionCardLimit(G, p);
    while (int(pl.actionCards.size()) > limit) {
        std::vector<Option> opts;
        for (int16_t c : pl.actionCards) {
            bool dup = false;
            for (const Option& o : opts)
                if (o.id == c) dup = true;
            if (!dup) opts.push_back({c, kActionCards[c].name, std::string(actionCardWhen(c)) + ": " + actionCardText(c)});
        }
        int c = pick(p, Tag::DiscardActionCard,
                     "Too many action cards: discard one (limit " + std::to_string(limit) + ")", opts);
        pl.actionCards.erase(std::find(pl.actionCards.begin(), pl.actionCards.end(), c));
        discardActionCard(p, c, false);
    }
}

void Rules::discardActionCard(int p, int card, bool played) {
    if (dataSkimmer(p, card)) {  // Data Skimmer (Ral Nel breakthrough): onto it instead
        if (!played) say(p, pn(p) + " discards " + kActionCards[card].name + ".");
        return;
    }
    if ((G.modules & kModDiscordant) && dsDiscarded(p, card, played)) return;  // Cybernetic Madness (purged), Skhot Unit X-12
    G.actionDiscard.push_back(int16_t(card));
    if (!played) say(p, pn(p) + " discards " + kActionCards[card].name + ".");
    // Reverse Engineer: take a discarded component-action card.
    if (!actionCardIsComponent(card)) return;
    for (int q : windowOrder()) {
        if (q == p || !canPlayActionCards(G, q) || ((silenced_ >> q) & 1)) continue;
        int re = -1;
        for (int16_t c : G.players[q].actionCards)
            if (isKey(c, "reverse_engineer")) re = c;
        if (re < 0) continue;
        Decision d;
        std::vector<Option> opts = {{re, "Reverse Engineer", std::string("Take ") + kActionCards[card].name + " into your hand."},
                                    {-1, "Don't play a card", ""}};
        int prev = curCard_;
        curCard_ = card;
        int c = pick(q, Tag::PlayActionCard, std::string("Play Reverse Engineer to take ") + kActionCards[card].name + "?", opts,
                     pn(p) + " just discarded it.");
        curCard_ = prev;
        if (c < 0) continue;
        if (!playActionCard(q, re, nullptr)) continue;
        auto it = std::find(G.actionDiscard.begin(), G.actionDiscard.end(), int16_t(card));
        if (it != G.actionDiscard.end()) {
            G.actionDiscard.erase(it);
            G.players[q].actionCards.push_back(int16_t(card));
            say(q, pn(q) + " takes " + kActionCards[card].name + " from the discard pile.");
            enforceHandLimit(q);
        }
        return;
    }
}

void Rules::discardAllActionCards(int p, const std::string& why) {
    Player& pl = G.players[p];
    if (pl.actionCards.empty()) return;
    std::vector<int16_t> cards = pl.actionCards;
    pl.actionCards.clear();
    say(p, pn(p) + " discards all " + std::to_string(cards.size()) + " action cards (" + why + ").");
    for (int16_t c : cards) G.actionDiscard.push_back(c);
}

bool Rules::cardHas(const std::vector<Played>& v, const char* key, int player) const {
    for (const Played& x : v)
        if (isKey(x.card, key) && (player < 0 || x.player == player)) return true;
    return false;
}

// ============================================================ timing windows
std::vector<int> Rules::windowOrder() const {
    // LRR 1.19 / 1.20: action phase - initiative order from the active player;
    // strategy / agenda phases - the speaker, then clockwise.
    std::vector<int> order;
    if (G.phase == Phase::Action && G.activePlayer >= 0) {
        order = initiativeOrder(G);
        auto it = std::find(order.begin(), order.end(), int(G.activePlayer));
        if (it != order.end()) std::rotate(order.begin(), it, order.end());
    } else {
        order = clockwiseFrom(G, G.speaker);
    }
    return order;
}

std::vector<Played> Rules::window(const WinCtx& c) {
    std::vector<Played> out;
    // E6-G0 Network (Uncharted Space relic, 0.43.0 U9): "at any time" - a person holding it is offered it at these
    // moments too (outside their own action menu), with "don't ask again this turn"
    if (G.fixes >= 2 && (G.modules & kModUncharted))
        for (int p = 0; p < G.numPlayers; ++p) {
            if (!G.players[size_t(p)].human || e6g0Quiet_[p] == G.turnCount || !relicReady(p, "e6-g0_network")) continue;
            if (G.phase == Phase::Action && G.activePlayer == p) continue;  // (on their own turn it is in the action menu)
            const int k = pick(p, Tag::Generic, "E6-G0 Network: exhaust it to draw 1 action card now?",
                               {{1, "Draw 1 action card", "The relic is exhausted until the status phase."},
                                {0, "Not now", ""},
                                {2, "Don't ask again this turn", ""}},
                               winText(c.w));
            if (k == 2) e6g0Quiet_[p] = G.turnCount;
            if (k == 1) e6g0(p);
        }
    bool anyCards = false;
    for (const Player& pl : G.players)
        if (!pl.actionCards.empty() && !pl.eliminated) anyCards = true;
    for (const PlanetState& ps : G.planets)
        if (!ps.salvage.empty()) anyCards = true;  // Dok 'N Pic's Salvage Yard (Thunder's Edge)
    if (!anyCards) return out;
    std::vector<int> order = windowOrder();
    std::vector<int> used;  // player*1000 + card: a player plays a card name once per window (LRR 2.6b)
    // Each player in turn plays as many cards as they want for this moment
    // (owner's ruling, RULES-CHECK 0.4.0 #2), then the next player responds.
    // If anyone played something, everyone gets another chance to respond.
    for (int pass = 0; pass < 6; ++pass) {
        bool any = false;
        for (int p : order) {
            for (int more = 0; more < 8; ++more) {
                const Player& pl = G.players[p];
                std::vector<int16_t> hand = pl.actionCards;
                for (int16_t s : salvageOf(p)) hand.push_back(s);  // (played as if in the hand)
                if (pl.eliminated || hand.empty() || !canPlayActionCards(G, p) || ((silenced_ >> p) & 1)) break;
                if (mutedFor_ >= 0 && p != mutedFor_) break;  // (fixes 27, Xxekir Grom) other players can't resolve abilities now
                std::vector<Option> opts;
                for (int16_t card : hand) {
                    bool dup = std::find(used.begin(), used.end(), p * 1000 + int(card)) != used.end();
                    for (const Option& o : opts)
                        if (o.id == card) dup = true;
                    if (dup || !cardPlayable(p, card, c)) continue;
                    opts.push_back({card, kActionCards[card].name, actionCardText(card)});
                }
                if (opts.empty()) break;
                opts.push_back({-1, more == 0 ? "Don't play a card" : "No more cards", ""});
                Decision d;
                d.player = p;
                d.kind = DecisionKind::Pick;
                d.tag = Tag::PlayActionCard;
                d.prompt = more == 0 ? "Play an action card?" : "Play another action card?";
                d.context = winText(c.w);
                d.options = opts;
                d.window = int(c.w);
                d.system = c.sys;
                d.planet = c.planet;
                d.targetPlayer = p == c.a ? c.b : c.a;
                d.value = c.value;
                d.agenda = curAgenda_;
                std::vector<Option> copy = opts;
                d.validate = [copy](const Answer& a) -> std::string {
                    if (a.picks.size() != 1) return "Choose one option.";
                    for (const Option& o : copy)
                        if (o.id == a.picks[0]) return "";
                    return "Not one of the options.";
                };
                int card = E.ask(std::move(d)).picks[0];
                if (card < 0) break;
                used.push_back(p * 1000 + card);
                any = true;
                if (playActionCard(p, card, &c)) out.push_back({p, card});
            }
        }
        if (!any) break;
    }
    return out;
}

bool Rules::cardPlayable(int p, int card, const WinCtx& c) const {
    // Man O' War (Florzen flagship, Discordant Stars): nobody else plays action cards in a space combat in its system
    if ((G.modules & kModDiscordant) && inSpaceCombat_ && G.activeSystem >= 0 && p >= 0 && p < G.numPlayers)
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && flagshipAbility(G, q, "florzen") && flagshipIn(G, q, G.activeSystem)) return false;
    // Stellar Atomics (galactic event): without the token, no action cards in the agenda phase
    if (G.phase == Phase::Agenda && eventOn(G, kEvAtomics) && p >= 0 && p < G.numPlayers && !G.players[size_t(p)].atomics) return false;
    if (usCard(card)) return usCardPlayable(p, card, c);  // Uncharted Space (0.33.0)
    if (teCard(card)) return teCardPlayable(p, card, c);  // Thunder's Edge (0.34.0)
    const char* k = kActionCards[card].key;
    const Player& pl = G.players[p];
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    switch (c.w) {
        case Win::StrategyStart:
            if (is("summit")) return reinforcementTokens(p) > 0;
            if (is("investments")) return true;
            return false;
        case Win::PickStrategy:
            return is("disgrace") && p != c.a;
        case Win::AfterActivate: {
            const SystemState& s = G.systems[c.sys];
            if (p == c.active) {
                if (is("fs") || is("nav_suite") || is("solar_flare")) return true;
                if (is("lost_star")) {
                    bool a = false, b = false;
                    for (size_t x = 0; x < G.systems.size(); ++x) {
                        if (wormholesOf(G, int(x)) & kAlpha) a = true;
                        if (wormholesOf(G, int(x)) & kBeta) b = true;
                    }
                    return a && b && !G.fx.lostStarChart;
                }
                if (is("silence_space")) return true;
                if (is("harness")) return s.d().anomalies && pl.commodities < commodityCap(G, pl);
                if (is("rally")) return otherShipsIn(G, p, c.sys) && reinforcementTokens(p) > 0;
                if (is("upgrade"))
                    return s.space[p].count(UnitType::Cruiser) > 0 && unitsAvailable(G, p, UnitType::Dreadnought) > 0;
                return false;
            }
            if (is("counterstroke")) return s.hasToken(p);
            if (is("fsb")) return hasUnits(G, p, c.sys);
            if (is("decoy")) {
                bool structures = false, mine = false;
                for (int16_t x : s.planets) {
                    if (G.planets[x].units[p].structures()) structures = true;
                    if (G.planets[x].owner == p) mine = true;
                }
                if (!structures || !mine) return false;
                if (G.fixes >= 27) {  // (fixes 27, the action cards review) "from the game board": space areas and the active
                    int mineHere = 0;   // system's other planets count too
                    for (int16_t x : s.planets)
                        if (G.planets[x].owner == p) ++mineHere;
                    for (const PlanetState& x : G.planets)
                        if (x.units[p].groundForces() && (x.system != c.sys || mineHere >= 2)) return true;
                    for (const SystemState& sy : G.systems)
                        if (sy.space[size_t(p)].groundForces()) return true;
                    return false;
                }
                for (const PlanetState& x : G.planets)
                    if (x.system != c.sys && x.units[p].groundForces()) return true;
                return false;
            }
            return false;
        }
        case Win::StrategicAction:
            return is("coup") && p != c.a;
        case Win::AfterAction:
            return is("master_plan") && p == c.active;
        case Win::AfterMove: {
            if (!is("experimental") || p == c.active || !hasShips(G, c.active, c.sys)) return false;
            if (G.fixes >= 25 && G.fx.myruThrough == c.active) return false;  // Myru Vos: no space cannon against those ships
            // (fixes 25, owner's faction debug pass) Solar Flare: "during this movement" - LRR 58.7 puts Space Cannon
            // Offense inside the movement step, and Battlestation fires right after the ships move, so it's covered too
            if (G.fixes >= 25 && G.fx.solarFlare) return false;
            auto dockIn = [&](int s) {
                if (G.systems[s].space[p].count(UnitType::SpaceDock)) return true;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[x].units[p].count(UnitType::SpaceDock)) return true;
                return false;
            };
            if (dockIn(c.sys)) return true;
            for (int16_t a : G.systems[c.sys].adj)
                if (dockIn(a)) return true;
            return false;
        }
        case Win::BeforeCannonHits:
            return is("mjets") && p == c.a && c.value > 0;
        case Win::AfterUnitRoll:
            // (fixes 27, the action cards review) "After another player makes a ... roll": anyone but the roller, not only
            // the target
            return is("scramble") && (G.fixes >= 27 ? p != c.a : p == c.b);
        case Win::CombatStart: {
            if (!is("reveal_prototype") || !participant(c, p) || spendable(G, p, false) < 4) return false;
            const Force& f = c.space ? G.systems[c.sys].space[p] : G.planets[c.planet].units[p];
            for (int t = 0; t < kTechCount; ++t) {
                if (!kTechs[t].unitUpgrade || !canResearchNow(G, p, t)) continue;
                for (int u = 0; u < kUnitCount; ++u)
                    if (kUnits[u].tech == t && f.n[int(kUnits[u].type)]) return true;
            }
            return false;
        }
        case Win::CombatRoundStart:
        case Win::CombatRoundEnd: {
            if (!participant(c, p)) return false;
            if (is("emergency")) {
                const Force& f = c.space ? G.systems[c.sys].space[p] : G.planets[c.planet].units[p];
                for (int t = 0; t < kUnitTypes; ++t)
                    if (f.dmg[t]) return true;
                for (int16_t x : G.systems[c.sys].planets)
                    for (int t = 0; t < kUnitTypes; ++t)
                        if (G.planets[x].units[p].dmg[t]) return true;
                return false;
            }
            if (c.w == Win::CombatRoundEnd) return false;
            if (is("mb")) return true;
            if (is("f_prototype")) return c.space && c.first && G.systems[c.sys].space[p].count(UnitType::Fighter) > 0;
            if (is("s_retreat")) {
                if (!c.space) return false;
                if (G.fixes >= 26 && noRetreat_) return false;  // (fixes 26, the Mahact review: Airo Shir Aur's combat - no ability moves ships)
                for (int16_t a : G.systems[c.sys].adj) {
                    uint8_t an = G.systems[a].d().anomalies;
                    if (G.fixes >= 12) {  // (0.44.8: Magmus Reactor allows a supernova)
                        if (!canEndMoveIn(G, p, a)) continue;
                    } else if ((an & kSupernova) || G.systems[a].isHyperlane()) continue;
                    if ((an & kAsteroid) && !owns(p, "amd")) continue;
                    if (!otherShipsIn(G, p, a)) return true;
                }
                return false;
            }
            return false;
        }
        case Win::BeforeAFB:
            return is("waylay") && p == c.a;
        case Win::AnnounceRetreats:
            return is("rout") && p == c.b && !retreatOptions(c.a, c.sys).empty();
        case Win::AfterRetreat:
            return is("intercept") && p != c.a && participant(c, p);
        case Win::BeforeSpaceHits:
            return is("sh") && p == c.a && c.value > 0;
        case Win::GroundHits:  // Wild, Wild Galaxy (galactic event): Shields Holding in ground combat too
            return is("sh") && eventOn(G, kEvWild) && p == c.a && c.value > 0;
        case Win::AfterSustain:
            if (is("dh")) return p == c.b && c.b >= 0 && (c.space || eventOn(G, kEvWild)) && !immuneToDirectHit(G, c.a, UnitType(c.value));
            if (is("reflective")) return p == c.a && c.space && inSpaceCombat_ && c.b >= 0 && shipsOrAlastor(c.b, c.sys);
            return false;
        case Win::ShipDestroyed:
            return is("courageous") && p == c.a && c.space && hasShips(G, c.b, c.sys);
        case Win::WonSpaceCombat:
            return is("salvage") && p == c.a && c.b >= 0 && c.b < G.numPlayers && G.players[c.b].commodities > 0;
        case Win::InvasionStart: {
            const SystemState& s = G.systems[c.sys];
            bool enemyPlanet = false, enemyPds = false, myPlanet = false;
            for (int16_t x : s.planets) {
                for (int q = 0; q < G.numPlayers; ++q) {
                    if (q == c.active) continue;
                    if (!G.planets[x].units[q].empty()) enemyPlanet = true;
                    if (G.planets[x].units[q].count(UnitType::PDS)) enemyPds = true;
                }
                if (G.planets[x].owner == p) myPlanet = true;
            }
            if (p == c.active) {
                if (is("disable")) return enemyPds;
                if (is("blitz")) {
                    if (!enemyPlanet) return false;
                    for (int t : kShipIdx)
                        if (t != int(UnitType::Fighter) && s.space[p].n[t] && unitDef(G, p, UnitType(t)).bombard <= 0) return true;
                    return false;
                }
                return false;
            }
            if (is("bunker")) {
                if (!myPlanet) return false;
                for (int t = 0; t < kUnitTypes; ++t)
                    if (s.space[c.active].n[t] && unitDef(G, c.active, UnitType(t)).bombard > 0) return true;
                return false;
            }
            return false;
        }
        case Win::AfterLanding:
            if (p != c.b) return false;
            if (is("parley")) return true;
            if (is("ghost_squad")) {
                for (int16_t x : G.systems[c.sys].planets)
                    if (x != c.planet && G.planets[x].owner == p && G.planets[x].units[p].groundForces()) return true;
                return false;
            }
            return false;
        case Win::AfterGroundRoll:
            return is("fire_team") && p == c.a && c.value > 0;
        case Win::GainControl:
            return is("infiltrate") && p == c.a && c.b >= 0 && G.planets[c.planet].units[c.b].structures() > 0;
        case Win::LoseControl:
            return is("reparations") && p == c.b;
        case Win::Production:
            return is("war_machine") && p == c.a;
        case Win::ReturnStrategyCards:
            return is("stability") && !pl.cards.empty();
        case Win::AgendaStart:
            return is("abs");
        case Win::AgendaRevealed:
            if (is("veto")) return true;
            if (is("dp")) {
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p && !G.players[q].eliminated && !G.players[q].notes.empty()) return true;
                return false;
            }
            return false;
        case Win::AfterAgendaRevealed:
            if (is("const_rider") || is("diplo_rider") || is("imp_rider") || is("lead_rider") || is("politic_rider") ||
                is("tech_rider") || is("trade_rider") || is("war_rider") || is("sanction"))
                return !((cannotVote_ >> p) & 1) || true;
            if (is("assassin") || is("insider")) return true;
            if (is("hack")) return lastVoter_ < 0;
            return false;
        case Win::AfterSpeakerVotes:
            return is("bribery") && pl.tradeGoods > 0 && int(G.votedFor.size()) > p && G.votedFor[p] != kNoVote;
        case Win::AfterCastVotes:
            return is("distinguished") && p == c.a;
        case Win::Elected:
            if (is("confusing")) return p == c.a;
            if (is("confounding")) return p != c.a;
            return false;
        case Win::OutcomeResolve: {
            if (!is("deadly_plot")) return false;
            bool other = int(G.votedFor.size()) > p && G.votedFor[p] != kNoVote && G.votedFor[p] != c.value;
            for (size_t i = 0; i + 1 < predictions_[p].size(); i += 2)
                if (predictions_[p][i + 1] != c.value) other = true;
            return other;
        }
        default: return false;
    }
}

bool Rules::playActionCard(int p, int card, const WinCtx* c) {
    Player& pl = G.players[p];
    auto it = std::find(pl.actionCards.begin(), pl.actionCards.end(), int16_t(card));
    bool salvaged = false;  // Dok 'N Pic's Salvage Yard (Thunder's Edge): purged instead of discarded
    if (it == pl.actionCards.end()) {
        if (!takeSalvage(p, card)) return false;
        salvaged = true;
    } else {
        pl.actionCards.erase(it);
    }
    auto discard = [&](bool resolved) {  // (resolved: Cybernetic Madness purges only cards that resolved, not cancelled ones)
        if (salvaged) {
            say(p, std::string(kActionCards[card].name) + " is purged (Dok 'N Pic's Salvage Yard).");
            return;
        }
        acCancelled_ = !resolved;
        discardActionCard(p, card, true);
        acCancelled_ = false;
    };
    {
        Event e;
        e.kind = EventKind::ActionCard;
        e.player = int8_t(p);
        e.card = int16_t(card);
        e.system = int16_t(c ? c->sys : -1);
        e.text = pn(p) + " plays " + kActionCards[card].name + ".";
        E.emit(e);
    }
    // Sabotage (LRR: "when another player plays an action card other than Sabotage")
    // (Black Market Dealings can't be cancelled; Nip and Tuck, the Last Bastion's commander: none of their cards)
    if (!isKey(card, "sabo") && !isKey(card, "blackmarketdealing") && !commanderUsable(G, p, "bastion")) {
        for (int q : windowOrder()) {
            if (q == p || G.players[q].eliminated || !canPlayActionCards(G, q) || ((silenced_ >> q) & 1)) continue;
            int sabo = -1;
            for (int16_t x : G.players[q].actionCards)
                if (isKey(x, "sabo")) sabo = x;
            if (sabo < 0) continue;
            int prev = curCard_, prevFor = curFor_;
            curCard_ = card;
            curFor_ = p;  // the AI sees who played it (Decision::targetPlayer)
            int ch = pick(q, Tag::PlayActionCard, std::string("Cancel ") + kActionCards[card].name + " with Sabotage?",
                          {{sabo, "Sabotage", std::string("Cancel ") + pn(p) + "'s " + kActionCards[card].name + "."},
                           {-1, "Don't play a card", ""}},
                          std::string(kActionCards[card].name) + ": " + actionCardText(card));
            curCard_ = prev;
            curFor_ = prevFor;
            if (ch < 0) continue;
            const int prevSab = sabotaged_;
            sabotaged_ = p * 1000 + card;  // (0.44.6: Instinct Training on the Sabotage sees what it would save)
            const bool played = playActionCard(q, sabo, nullptr);
            sabotaged_ = prevSab;
            if (!played) continue;
            say(p, std::string(kActionCards[card].name) + " is cancelled.");
            if (eventOn(G, kEvWild) && !salvaged) {  // Wild, Wild Galaxy: the saboteur takes the card
                G.players[size_t(q)].actionCards.push_back(int16_t(card));
                say(q, pn(q) + " takes " + kActionCards[card].name + " (Wild, Wild Galaxy).");
                enforceHandLimit(q);
                return false;
            }
            discard(false);
            return false;
        }
    }
    // Instinct Training (Xxcha): cancel it for a strategy token (0.44.6, fixes 10: not Black Market Dealings - it can't be
    // cancelled)
    if (!(G.fixes >= 10 && isKey(card, "blackmarketdealing")) && cancelActionCard(p, card)) {
        say(p, std::string(kActionCards[card].name) + " is cancelled.");
        discard(false);
        return false;
    }
    int prev = curCard_;
    curCard_ = card;
    resolveCard(p, card, c);
    curCard_ = prev;
    discard(true);
    return true;
}

// ============================================================ card effects
void Rules::resolveCard(int p, int card, const WinCtx* c) {
    Player& pl = G.players[p];
    const char* k = kActionCards[card].key;
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    const int sys = c ? c->sys : -1;
    if (usResolve(p, card, c)) return;  // Uncharted Space (0.33.0)
    if (teResolve(p, card, c)) return;  // Thunder's Edge (0.34.0)

    // ---------- effects handled by the code that opened the window
    if (is("sabo") || is("reverse_engineer") || is("disgrace") || is("coup") || is("master_plan") || is("mjets") ||
        is("scramble") || is("mb") || is("f_prototype") || is("waylay") || is("rout") || is("intercept") || is("sh") ||
        is("parley") || is("fire_team") || is("infiltrate") || is("war_machine") || is("stability") || is("veto") ||
        is("hack") || is("confusing") || is("confounding") || is("deadly_plot"))
        return;

    // ---------- strategy phase
    if (is("summit")) {
        gainTokens(p, 2, false, "Summit: place 2 command tokens.");
        return;
    }
    if (is("investments")) {
        std::vector<Option> opts;
        for (int s = 0; s < kStrategyCards; ++s)
            if (G.cardOwner[s] < 0)
                opts.push_back({s, std::to_string(s + 1) + " " + strategyName(s),
                                std::to_string(G.cardTradeGoods[s]) + " trade goods on it now"});
        if (opts.size() < 3) return;
        if (G.fixes >= 27) {  // (fixes 27, the action cards review) any split over at least 3 cards, e.g. 3 / 1 / 1
            std::vector<int> placed;
            for (int i = 0; i < 5; ++i) {
                int distinct = 0;
                for (size_t k = 0; k < placed.size(); ++k)
                    if (std::find(placed.begin(), placed.begin() + long(k), placed[k]) == placed.begin() + long(k)) ++distinct;
                const bool mustNew = 3 - distinct >= 5 - i;  // (the rest must reach 3 different cards)
                std::vector<Option> o2;
                for (const Option& o : opts) {
                    Option x = o;
                    x.detail = std::to_string(G.cardTradeGoods[o.id]) + " trade goods on it now";
                    if (mustNew && std::find(placed.begin(), placed.end(), o.id) != placed.end()) x.enabled = false;
                    o2.push_back(x);
                }
                const int prevV = curValue_;
                curValue_ = i;  // (for the AI: which trade good this is)
                const int s = pick(p, Tag::CardTarget, "Manipulate Investments: trade good " + std::to_string(i + 1) + " of 5 - on which strategy card?",
                                   o2, "At least 3 different cards in all.");
                curValue_ = prevV;
                G.cardTradeGoods[s]++;
                placed.push_back(s);
            }
            say(p, pn(p) + " places 5 trade goods on strategy cards.");
            return;
        }
        std::vector<int> cards = pickMany(p, Tag::CardTarget, "Manipulate Investments: choose 3 to 5 strategy cards", opts, 3,
                                          std::min<int>(5, int(opts.size())),
                                          "5 trade goods are spread over them, the first ones you pick get the extra.");
        for (int i = 0; i < 5; ++i) G.cardTradeGoods[cards[i % cards.size()]]++;
        say(p, pn(p) + " places 5 trade goods on strategy cards.");
        return;
    }

    // ---------- tactical action: after activation
    if (is("fs")) {
        G.fx.flankSpeed = true;
        return;
    }
    if (is("nav_suite")) {
        G.fx.navSuite = true;
        return;
    }
    if (is("solar_flare")) {
        G.fx.solarFlare = true;
        return;
    }
    if (is("lost_star")) {
        G.fx.lostStarChart = true;
        computeAdjacency(G);
        return;
    }
    if (is("silence_space")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (int(s) != sys && G.systems[s].space[p].ships() && !G.systems[s].hasToken(p))
                opts.push_back({int(s), sysName(int(s)), ""});
        if (opts.empty()) return;
        G.fx.silenceSystem = int16_t(pick(p, Tag::CardTarget, "In the Silence of Space: which system's ships?", opts,
                                          "They may move through systems with other players' ships.", true));
        return;
    }
    if (is("harness")) {
        const int before = pl.commodities;
        pl.commodities = int8_t(refreshTo(G, pl));
        say(p, pn(p) + " refills commodities to " + std::to_string(pl.commodities) + ".");
        replenished(p, pl.commodities - before);  // Minister of Commerce, Trade Agreement (0.21.1 audit)
        return;
    }
    if (is("rally")) {
        int n = std::min(2, reinforcementTokens(p));
        pl.fleet = int8_t(pl.fleet + n);
        say(p, pn(p) + " adds " + std::to_string(n) + " tokens to the fleet pool.");
        return;
    }
    if (is("upgrade")) {
        Force& f = G.systems[sys].space[p];
        bool damaged = f.dmg[int(UnitType::Cruiser)] >= f.n[int(UnitType::Cruiser)];
        f.remove(UnitType::Cruiser, 1, false);
        addUnits(p, UnitType::Dreadnought, 1, sys, -1);
        (void)damaged;
        say(p, pn(p) + " upgrades a cruiser to a dreadnought.", sys);
        return;
    }
    if (is("counterstroke")) {
        G.systems[sys].tokens &= uint16_t(~(1u << p));
        pl.tactic = int8_t(pl.tactic + 1);
        say(p, pn(p) + " takes the command token in " + sysName(sys) + " back to the tactic pool.", sys);
        return;
    }
    if (is("fsb")) {
        gainTG(p, 3);
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated) opts.push_back({q, pn(q), ""});
        int q = pick(p, Tag::CardTarget, "Forward Supply Base: who gains 1 trade good?", opts);
        gainTG(q, 1);
        say(p, pn(p) + " gains 3 trade goods; " + pn(q) + " gains 1.");
        return;
    }
    if (is("decoy")) {
        std::vector<Option> dest;
        for (int16_t x : G.systems[sys].planets)
            if (G.planets[x].owner == p) dest.push_back({x, planetName(x), ""});
        int to = dest.size() == 1 ? dest[0].id : pick(p, Tag::CardTarget, "Decoy Operation: move ground forces to which planet?", dest);
        std::vector<Option> units;
        const int np = int(G.planets.size());
        for (int x = 0; x < np; ++x) {
            if (G.planets[x].system == sys && (G.fixes < 27 || x == to)) continue;  // (fixes 27: the system's other planets too)
            const Force& f = G.planets[x].units[p];
            for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                for (int i = 0; i < f.count(t) && i < 2; ++i)
                    units.push_back({x * 64 + int(t) * 4 + i, std::string(unitTypeName(t)) + " on " + planetName(x), ""});
        }
        for (size_t sy = 0; sy < G.systems.size(); ++sy) {  // ground forces in space areas too (0.21.1 audit)
            const Force& f = G.systems[sy].space[p];
            for (UnitType t : {UnitType::Infantry, UnitType::Mech})
                for (int i = 0; i < f.count(t) && i < 2; ++i)
                    units.push_back({(np + int(sy)) * 64 + int(t) * 4 + i,
                                     std::string(unitTypeName(t)) + " in space, " + sysName(int(sy)), ""});
        }
        if (units.empty()) return;
        std::vector<int> picks = pickMany(p, Tag::CardTarget, "Decoy Operation: choose up to 2 ground forces", units, 0,
                                          std::min<int>(2, int(units.size())));
        for (int id : picks) {
            int x = id / 64, t = (id % 64) / 4;
            if (x >= np) G.systems[x - np].space[p].remove(UnitType(t), 1, false);
            else G.planets[x].units[p].remove(UnitType(t), 1, false);
            G.planets[to].units[p].add(UnitType(t), 1);
        }
        if (!picks.empty()) say(p, pn(p) + " moves " + std::to_string(picks.size()) + " ground forces to " + planetName(to) + ".", sys);
        return;
    }
    if (is("experimental")) {
        std::vector<Option> opts;
        auto addDocks = [&](int s) {
            if (G.systems[s].space[p].count(UnitType::SpaceDock)) opts.push_back({s * 100 + 99, "Floating factory in " + sysName(s), ""});
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].units[p].count(UnitType::SpaceDock)) opts.push_back({s * 100, "Space dock on " + planetName(x), ""});
        };
        addDocks(sys);
        for (int16_t a : G.systems[sys].adj) addDocks(a);
        if (opts.empty()) return;
        if (opts.size() > 1) pick(p, Tag::CardTarget, "Experimental Battlestation: which space dock fires?", opts);
        // 0.44.5 (fixes 9): a space cannon shot - Graviton Laser System and Antimass Deflectors apply
        const bool gls = G.fixes >= 9 && askGraviton(p, c->active, sys);
        // (fixes 25, owner's faction debug pass) Assail: +1 against a puppet (a unit ability roll)
        const int need = 5 + (G.fixes >= 9 ? antimass(p, c->active) : 0) - (G.fixes >= 25 && (G.modules & kModThundersEdge) ? assail(p, c->active) : 0);
        RollResult r;
        // (0.44.8, fixes 12) the dock uses SPACE CANNON: Plasma Scoring and the other extra dice apply
        const int dice = 3 + (G.fixes >= 12 ? abilityDice(p, sys) : 0);
        for (int i = 0; i < dice; ++i) {
            int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            r.need.push_back(int8_t(need));
            if (v >= need) ++r.hits;
        }
        reportRoll(p, sys, "Experimental Battlestation at " + pn(c->active), r);
        if (r.hits) cannonHits(c->active, sys, r.hits, kShipMaskAll, gls, "Experimental Battlestation", p);
        return;
    }

    // ---------- combat
    if (is("reveal_prototype")) {
        // (fixes 27, the Nekro review - owner 2026-11-14) Propagation: "When you would research a technology: Gain 3 command
        // tokens instead" - the 4 resources, then the tokens
        if (G.fixes >= 27 && fac(p, "nekro")) {
            if (spendable(G, p, false) >= 4) propagation(p, 4);
            return;
        }
        const Force& f = c->space ? G.systems[sys].space[p] : G.planets[c->planet].units[p];
        std::vector<Option> opts;
        // (0.44.9, fixes 13 - the owner's answer C11: Inheritance Systems here too - +2 resources, every prerequisite ignored)
        const bool inherit = G.fixes >= 13 && techReady(p, "is") && spendable(G, p, false) >= 6;
        std::vector<int> viaInherit;
        for (int t = 0; t < kTechCount; ++t) {
            if (!kTechs[t].unitUpgrade) continue;
            const bool now = canResearchNow(G, p, t);
            if (!now && !(inherit && techAvailableToResearch(G, p, t))) continue;
            bool fits = false;
            for (int u = 0; u < kUnitCount; ++u)
                if (kUnits[u].tech == t && f.n[int(kUnits[u].type)]) fits = true;
            if (!fits) continue;
            if (!now) viaInherit.push_back(t);
            opts.push_back({t, kTechs[t].name, std::string(now ? "" : "(uses Inheritance Systems: +2 resources) ") + techSummary(t)});
        }
        if (opts.empty()) return;
        int t = pick(p, Tag::CardTarget, "Reveal Prototype: research which unit upgrade?", opts, "Costs 4 resources.");
        const bool useIs = std::find(viaInherit.begin(), viaInherit.end(), t) != viaInherit.end();
        if (useIs) {
            exhaustTech(p, "is");
            say(p, pn(p) + " exhausts Inheritance Systems to ignore every prerequisite.");
        }
        if (pay(p, useIs ? 6 : 4, false, "Reveal Prototype", false)) {
            researchTech(p, t);
            E.log(p, pn(p) + " researches " + kTechs[t].name + ".", EventKind::Research);
        }
        return;
    }
    if (is("emergency")) {
        auto fix = [](Force& f) {
            for (int t = 0; t < kUnitTypes; ++t) f.dmg[t] = 0;
        };
        fix(G.systems[sys].space[p]);
        for (int16_t x : G.systems[sys].planets) fix(G.planets[x].units[p]);
        say(p, pn(p) + " repairs all of their units in " + sysName(sys) + ".", sys);
        return;
    }
    if (is("s_retreat")) {
        std::vector<Option> opts;
        for (int16_t a : G.systems[sys].adj) {
            uint8_t an = G.systems[a].d().anomalies;
            if (G.fixes >= 12) {  // (0.44.8: Magmus Reactor allows a supernova)
                if (!canEndMoveIn(G, p, a)) continue;
            } else if ((an & kSupernova) || G.systems[a].isHyperlane()) continue;
            if ((an & kAsteroid) && !owns(p, "amd")) continue;
            if (!otherShipsIn(G, p, a)) opts.push_back({a, sysName(a), ""});
        }
        if (opts.empty()) return;
        int to = opts.size() == 1 ? opts[0].id : pick(p, Tag::CardTarget, "Skilled Retreat: move to which system?", opts, "", true);
        noRetreatToken_ = eventOn(G, kEvWild);  // Wild, Wild Galaxy: no command token
        doRetreat(p, sys, to);
        noRetreatToken_ = false;
        combatDraw_ = true;
        return;
    }
    if (is("dh")) {
        // c->value = unit type that sustained damage
        destroyUnits(c->a, UnitType(c->value), 1, sys, c->space ? -1 : c->planet, p, "Direct Hit");
        return;
    }
    if (is("reflective")) {
        assignHits(c->b, sys, -1, 2, kShipMaskAll | alastorMask(c->b), "Reflective Shielding", p);
        return;
    }
    if (is("courageous")) {
        int need = unitDef(G, p, UnitType(c->value)).combat;
        RollResult r;
        for (int i = 0; i < 2; ++i) {
            int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            r.need.push_back(int8_t(need));
            if (v >= need) ++r.hits;
        }
        reportRoll(p, sys, "Courageous to the End", r);
        if (r.hits) {
            int opp = c->b;
            int n = std::min(r.hits, G.systems[sys].space[opp].ships());
            if (n > 0) chooseRemovals(opp, sys, n, kShipMaskAll, "Courageous to the End");
        }
        return;
    }
    if (is("salvage")) {
        Player& lo = G.players[c->b];
        if ((G.modules & kModDiscordant) && breakthroughsOn(G) && dsbtVault(c->b, p, "their commodities")) return;  // (0.43.0)
        int n = lo.commodities;
        lo.commodities = 0;
        gainTG(p, n);
        say(p, pn(p) + " salvages " + std::to_string(n) + " commodities from " + pn(c->b) + " (as trade goods).");
        return;
    }
    if (is("blitz")) {
        blitz_ = true;
        return;
    }
    if (is("bunker")) return;  // the invasion code remembers who played it
    if (is("disable")) {
        disabled_ = true;
        return;
    }
    if (is("ghost_squad") && G.fixes >= 27) {
        // (fixes 27, the action cards review) "Move any number of your ground forces from any planet you control in the active
        // system to any other planet you control in the active system": a move at a time - from, to, how many
        const int prevV = curValue_;
        curValue_ = c->planet;  // (for the AI: the planet being invaded)
        for (int guard = 0; guard < 8; ++guard) {
            std::vector<Option> from;
            for (int16_t x : G.systems[sys].planets)
                if (G.planets[x].owner == p && G.planets[x].units[p].groundForces())
                    from.push_back({x, planetName(x), std::to_string(G.planets[x].units[p].groundForces()) + " ground forces"});
            if (from.empty()) break;
            from.push_back({-1, "Done", "No more moves."});
            const int x = pick(p, Tag::CardTarget, "Ghost Squad: move from which planet?", from, "Any number, to another planet of yours in this system.");
            if (x < 0) break;
            std::vector<Option> to;
            for (int16_t y : G.systems[sys].planets)
                if (y != x && G.planets[y].owner == p) to.push_back({y, planetName(y), ""});
            if (to.empty()) break;
            const int y = to.size() == 1 ? to[0].id : pick(p, Tag::CardTarget, "Ghost Squad: move to which planet?", to);
            int moved = 0;
            for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
                const int n = G.planets[x].units[p].count(t);
                if (!n) continue;
                std::vector<Option> k;
                for (int i = 0; i <= n; ++i) k.push_back({i, std::to_string(i), ""});
                const int m = pick(p, Tag::CardTarget, std::string("Ghost Squad: how many ") + (t == UnitType::Infantry ? "infantry" : "mechs") + "?", k,
                                   "From " + planetName(x) + " to " + planetName(y) + ".");
                if (m <= 0) continue;
                G.planets[x].units[p].remove(t, m, false);
                G.planets[y].units[p].add(t, m);
                moved += m;
            }
            if (moved) say(p, pn(p) + " moves " + std::to_string(moved) + " ground forces from " + planetName(x) + " to " + planetName(y) + " (Ghost Squad).", sys);
        }
        curValue_ = prevV;
        return;
    }
    if (is("ghost_squad")) {
        std::vector<Option> opts;
        for (int16_t x : G.systems[sys].planets)
            if (x != c->planet && G.planets[x].owner == p && G.planets[x].units[p].groundForces())
                opts.push_back({x, planetName(x), std::to_string(G.planets[x].units[p].groundForces()) + " ground forces"});
        std::vector<int> from = pickMany(p, Tag::CardTarget, "Ghost Squad: move all ground forces from which planets to " +
                                                                 planetName(c->planet) + "?",
                                         opts, 0, int(opts.size()));
        for (int x : from)
            for (UnitType t : {UnitType::Infantry, UnitType::Mech}) {
                int n = G.planets[x].units[p].count(t);
                G.planets[x].units[p].remove(t, n, false);
                G.planets[c->planet].units[p].add(t, n);
            }
        return;
    }
    if (is("reparations")) {
        std::vector<Option> theirs, mine;
        for (int x : controlledPlanets(G, c->a))
            if (!G.planets[x].exhausted) theirs.push_back({x, planetName(x), ""});
        for (int x : controlledPlanets(G, p))
            if (G.planets[x].exhausted) mine.push_back({x, planetName(x), ""});
        if (!theirs.empty()) {
            int x = pick(p, Tag::CardTarget, "Reparations: exhaust which of " + pn(c->a) + "'s planets?", theirs, "", false, true);
            G.planets[x].exhausted = true;
        }
        if (!mine.empty()) {
            int x = pick(p, Tag::CardTarget, "Reparations: ready which of your planets?", mine, "", false, true);
            G.planets[x].exhausted = false;
        }
        return;
    }

    // ---------- agenda phase
    if (is("abs")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[q].eliminated) {
                int n = 0;
                for (int x : controlledPlanets(G, q))
                    if (hasTraitOf(G, x, Trait::Cultural) && !G.planets[x].exhausted) ++n;
                opts.push_back({q, pn(q), std::to_string(n) + " ready cultural planets"});
            }
        int q = pick(p, Tag::CardTarget, "Ancient Burial Sites: exhaust whose cultural planets?", opts);
        for (int x : controlledPlanets(G, q))
            if (hasTraitOf(G, x, Trait::Cultural)) G.planets[x].exhausted = true;
        say(p, pn(q) + "'s cultural planets are exhausted.");
        return;
    }
    if (is("dp")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && !G.players[q].notes.empty())
                opts.push_back({q, pn(q), std::to_string(G.players[q].notes.size()) + " promissory notes"});
        if (opts.empty()) return;
        int q = pick(p, Tag::CardTarget, "Diplomatic Pressure: who must give you a promissory note?", opts);
        std::vector<Option> notes = notesToGive(q, p);  // Hubris: never the Alliance to Mahact
        if (notes.empty()) return;
        for (int k = 0; k < (eventOn(G, kEvWild) ? 3 : 1); ++k) {  // Wild, Wild Galaxy: 3 notes
            if (k) notes = notesToGive(q, p);
            if (notes.empty()) break;
            int prev = curCard_;
            curCard_ = -1;
            int n = pick(q, Tag::GiveNote, "Diplomatic Pressure: give " + pn(p) + " which promissory note?", notes);
            curCard_ = prev;
            giveNote(q, p, n);
        }
        return;
    }
    if (is("assassin")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (!G.players[q].eliminated && !((cannotVote_ >> q) & 1)) opts.push_back({q, pn(q), ""});
        if (opts.empty()) return;
        int q = pick(p, Tag::CardTarget, "Assassinate Representative: who may not vote on this agenda?", opts);
        cannotVote_ = uint8_t(cannotVote_ | (1u << q));
        say(p, pn(q) + " may not vote on this agenda.");
        return;
    }
    if (is("insider")) {
        std::vector<Option> opts;
        for (int i = 0; i < 3 && i < int(G.agendaDeck.size()); ++i) {
            int a = G.agendaDeck[G.agendaDeck.size() - 1 - i];
            opts.push_back({i, std::to_string(i + 1) + ". " + kAgendas[a].name, agendaText(a, false)});
        }
        E.revealed();
        if (!opts.empty()) pick(p, Tag::CardTarget, "Insider Information: the next agendas, top first", opts, "Press A to continue.");
        return;
    }
    if (is("bribery")) {
        std::vector<Option> opts;
        for (int n = 0; n <= pl.tradeGoods; ++n) opts.push_back({n, std::to_string(n) + " trade goods", std::to_string(n) + " more votes"});
        int n = pick(p, Tag::CardTarget, "Bribery: spend how many trade goods for extra votes?", opts);
        pl.tradeGoods = int8_t(pl.tradeGoods - n);
        G.voteCount[p] += n;
        say(p, pn(p) + " adds " + std::to_string(n) + " votes.");
        return;
    }
    if (is("distinguished")) {
        G.voteCount[p] += 5;
        say(p, pn(p) + " casts 5 additional votes.");
        return;
    }
    if (is("const_rider") || is("diplo_rider") || is("imp_rider") || is("lead_rider") || is("politic_rider") ||
        is("tech_rider") || is("trade_rider") || is("war_rider") || is("sanction")) {
        std::vector<Option> opts = agendaOutcomes(curAgenda_);
        int o = pick(p, Tag::Predict, std::string(kActionCards[card].name) + ": predict the outcome", opts,
                     "You can't vote on this agenda. If your prediction is right you get the reward.");
        predictions_[p].push_back(card);
        predictions_[p].push_back(o);
        cannotVote_ = uint8_t(cannotVote_ | (1u << p));
        say(p, pn(p) + " predicts: " + outcomeName(curAgenda_, o) + ".");
        return;
    }

    // ---------- component actions
    if (is("arch_expedition")) {
        std::vector<Option> opts;
        bool seen[3] = {};
        for (int x : controlledPlanets(G, p))
            for (int deck = 0; deck < 3; ++deck) {  // (Terraform: a planet with all three traits)
                if (seen[deck] || !hasTraitOf(G, x, deck == 0 ? Trait::Cultural : deck == 1 ? Trait::Hazardous : Trait::Industrial)) continue;
                seen[deck] = true;
                opts.push_back({deck, std::string(exploreTypeName(ExploreType(deck))) + " deck", ""});
            }
        if (opts.empty()) return;
        int deck = opts.size() == 1 ? opts[0].id
                                    : pick(p, Tag::CardTarget, "Archaeological Expedition: reveal the top 3 cards of which deck?", opts);
        int got = 0;
        std::string shown;
        for (int i = 0; i < 3; ++i) {
            int c = drawExplore(deck);
            if (c < 0) break;
            shown += std::string(shown.empty() ? "" : ", ") + kExploreCards[c].name;
            if (kExploreCards[c].kind == ExploreKind::Fragment) {
                pl.fragments.push_back(int16_t(c));
                ++got;
            } else {
                G.exploreDiscard[deck].push_back(int16_t(c));
            }
        }
        say(p, pn(p) + " reveals " + shown + " and keeps " + std::to_string(got) + " relic fragment" + (got == 1 ? "" : "s") + ".");
        return;
    }
    if (is("probe")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (!G.systems[s].frontier) continue;
            bool near = hasShips(G, p, int(s));
            for (int16_t a : G.systems[s].adj)
                if (hasShips(G, p, a)) near = true;
            if (near) opts.push_back({int(s), sysName(int(s)), "frontier token"});
        }
        if (opts.empty()) return;
        int s = pick(p, Tag::CardTarget, "Exploration Probe: explore which frontier token?", opts, "", true);
        exploreFrontier(p, s);
        return;
    }
    if (is("seize")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && fragmentCount(G, q) > 0 && areNeighbours(G, p, q))
                opts.push_back({q, pn(q), std::to_string(fragmentCount(G, q)) + " relic fragments"});
        if (opts.empty()) return;
        int q = pick(p, Tag::CardTarget, "Seize Artifact: take a relic fragment from which neighbour?", opts);
        if (vaultShut(q, p, "a relic fragment")) return;  // (High Security Vault)
        std::vector<Option> frs;
        for (size_t i = 0; i < G.players[q].fragments.size(); ++i)
            if (isRelicFragment(G.players[q].fragments[i])) frs.push_back({int(i), kExploreCards[G.players[q].fragments[i]].name, ""});
        int i = pick(p, Tag::CardTarget, "Seize Artifact: take which fragment?", frs);
        int16_t f = G.players[q].fragments[size_t(i)];
        G.players[q].fragments.erase(G.players[q].fragments.begin() + i);
        pl.fragments.push_back(f);
        say(p, pn(p) + (std::strchr("AEIOU", kExploreCards[f].name[0]) ? " takes an " : " takes a ") + kExploreCards[f].name + " from " +
                   pn(q) + ".");
        return;
    }
    if (is("cripple")) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            int n = 0;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p) n += G.planets[x].units[q].count(UnitType::PDS);
            if (n) opts.push_back({int(x), planetName(int(x)), std::to_string(n) + " PDS"});
        }
        int x = pick(p, Tag::CardTarget, "Cripple Defenses: destroy the PDS on which planet?", opts, "", false, true);
        for (int q = 0; q < G.numPlayers; ++q)
            if (G.planets[x].units[q].count(UnitType::PDS))
                destroyUnits(q, UnitType::PDS, G.planets[x].units[q].count(UnitType::PDS), G.planets[x].system, x, p, "Cripple Defenses");
        return;
    }
    if (is("divert_funding")) {
        std::vector<Option> opts;
        for (int16_t t : pl.techs)
            if (!kTechs[t].unitUpgrade && kTechs[t].faction < 0) opts.push_back({t, kTechs[t].name, techSummary(t)});
        int t = pick(p, Tag::CardTarget, "Divert Funding: return which technology?", opts);
        pl.techs.erase(std::find(pl.techs.begin(), pl.techs.end(), int16_t(t)));
        auto ex = std::find(pl.exhaustedTechs.begin(), pl.exhaustedTechs.end(), int16_t(t));
        if (ex != pl.exhaustedTechs.end()) pl.exhaustedTechs.erase(ex);
        say(p, pn(p) + " returns " + kTechs[t].name + ".");
        researchStep(p, 0, "Divert Funding", t);
        return;
    }
    if (is("economic_initiative")) {
        for (int x : controlledPlanets(G, p))
            if (hasTraitOf(G, x, Trait::Cultural)) G.planets[x].exhausted = false;
        say(p, pn(p) + " readies all cultural planets.");
        return;
    }
    if (is("f_conscription")) {
        int placed = 0;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (otherShipsIn(G, p, int(s))) continue;
            bool ok = G.systems[s].space[p].count(UnitType::SpaceDock) > 0;
            for (int t : kShipIdx)
                if (G.systems[s].space[p].n[t] && unitDef(G, p, UnitType(t)).capacity > 0) ok = true;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].units[p].count(UnitType::SpaceDock)) ok = true;
            if (!ok) continue;
            addUnits(p, UnitType::Fighter, 1, int(s), -1);
            ++placed;
            removeExcessCapacity(p, int(s));
        }
        say(p, pn(p) + " places " + std::to_string(placed) + " fighters.");
        return;
    }
    if (is("f_deployment")) {
        std::vector<Option> opts;
        for (int x : controlledPlanets(G, p)) opts.push_back({x, planetName(x), ""});
        int x = pick(p, Tag::CardTarget, "Frontline Deployment: place 3 infantry on which planet?", opts, "", false, true);
        addUnits(p, UnitType::Infantry, 3, G.planets[x].system, x);
        say(p, pn(p) + " places 3 infantry on " + planetName(x) + ".", G.planets[x].system);
        return;
    }
    if (is("f_researched")) {
        pl.tradeGoods = int8_t(pl.tradeGoods - 4);
        researchStep(p, 0, "Focused Research");
        return;
    }
    if (is("ghost_ship")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].home < 0 && (wormholesOf(G, s) & (kAlpha | kBeta | kGamma | kDelta)) &&
                !otherShipsIn(G, p, int(s)) && !G.systems[s].offBoard)
                opts.push_back({int(s), sysName(int(s)), ""});
        if (opts.empty()) return;
        int s = pick(p, Tag::CardTarget, "Ghost Ship: place a destroyer where?", opts, "", true);
        addUnits(p, UnitType::Destroyer, 1, s, -1);
        say(p, pn(p) + " places a destroyer in " + sysName(s) + ".", s);
        return;
    }
    if (is("impersonation")) {
        if (pay(p, 3, true, "Impersonation", false)) drawSecret(p);
        return;
    }
    if (is("industrial_initiative")) {
        int n = 0;
        for (int x : controlledPlanets(G, p))
            if (hasTraitOf(G, x, Trait::Industrial)) ++n;
        gainTG(p, n);
        say(p, pn(p) + " gains " + std::to_string(n) + " trade goods.");
        return;
    }
    if (is("insub")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.players[q].tactic > 0 && !G.players[q].eliminated)
                opts.push_back({q, pn(q), std::to_string(G.players[q].tactic) + " tactic tokens"});
        int q = pick(p, Tag::CardTarget, "Insubordination: who loses a tactic token?", opts);
        G.players[q].tactic = int8_t(G.players[q].tactic - 1);
        say(p, pn(q) + " loses a tactic token.");
        return;
    }
    if (is("jamming")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (G.systems[s].home >= 0 || G.systems[s].isHyperlane() || G.systems[s].offBoard) continue;
            bool near = hasShips(G, p, int(s));
            for (int16_t a : G.systems[s].adj)
                if (hasShips(G, p, a)) near = true;
            if (near) opts.push_back({int(s), sysName(int(s)), ""});
        }
        int s = pick(p, Tag::CardTarget, "Signal Jamming: which system?", opts, "", true);
        std::vector<Option> who;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && !G.systems[s].hasToken(q)) who.push_back({q, pn(q), ""});
        if (who.empty()) return;
        int q = pick(p, Tag::CardTarget, "Signal Jamming: whose command token goes there?", who);
        placeTokenFromReinforcements(q, s, "Signal Jamming");
        say(p, pn(q) + " gets a command token in " + sysName(s) + ".", s);
        return;
    }
    if (is("lucky")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            bool mine = false;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].owner == p) mine = true;
            if (!mine) continue;
            for (int q = 0; q < G.numPlayers; ++q) {
                if (q == p) continue;
                for (UnitType t : {UnitType::Dreadnought, UnitType::Cruiser, UnitType::Destroyer})
                    if (G.systems[s].space[q].count(t))
                        opts.push_back({int(s) * 1000 + q * 16 + int(t), pn(q) + "'s " + unitTypeName(t) + " in " + sysName(int(s)), ""});
            }
        }
        int id = pick(p, Tag::CardTarget, "Lucky Shot: destroy which ship?", opts);
        // 0.33.0 fix: the unit type is (id % 1000) % 16 - id % 16 misread odd-numbered systems (1000 isn't a
        // multiple of 16), and the card did nothing there. Games started before keep that (Setup::fixes).
        const int t = G.fixes >= 1 ? (id % 1000) % 16 : id % 16;
        if (t >= kUnitTypes) return;
        destroyUnits((id % 1000) / 16, UnitType(t), 1, id / 1000, -1, p, "Lucky Shot");
        return;
    }
    if (is("meltdown")) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            if (G.systems[G.planets[x].system].home >= 0) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && G.planets[x].units[q].count(UnitType::SpaceDock))
                    opts.push_back({int(x) * 16 + q, pn(q) + "'s space dock on " + planetName(int(x)), ""});
        }
        for (size_t sy = 0; sy < G.systems.size(); ++sy) {  // floating factories (Saar) count too (0.21.1 audit)
            if (G.systems[sy].home >= 0) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && G.systems[sy].space[q].count(UnitType::SpaceDock))
                    opts.push_back({50000 + int(sy) * 16 + q, pn(q) + "'s floating factory in " + sysName(int(sy)), ""});
        }
        int id = pick(p, Tag::CardTarget, "Reactor Meltdown: destroy which space dock?", opts);
        if (id >= 50000) {
            int sy = (id - 50000) / 16, q = (id - 50000) % 16;
            destroyUnits(q, UnitType::SpaceDock, 1, sy, -1, p, "Reactor Meltdown");
            removeExcessCapacity(q, sy);
            return;
        }
        int x = id / 16, q = id % 16;
        destroyUnits(q, UnitType::SpaceDock, 1, G.planets[x].system, x, p, "Reactor Meltdown");
        return;
    }
    if (is("messiah")) {
        int n = 0;
        for (int x : controlledPlanets(G, p)) {
            addUnits(p, UnitType::Infantry, 1, G.planets[x].system, x);
            ++n;
        }
        say(p, pn(p) + " places " + std::to_string(n) + " infantry.");
        return;
    }
    if (is("mining_initiative")) {
        int best = 0;
        for (int x : controlledPlanets(G, p)) best = std::max(best, planetResources(G, x));
        gainTG(p, best);
        say(p, pn(p) + " gains " + std::to_string(best) + " trade goods.");
        return;
    }
    if (is("plagiarize")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || !areNeighbours(G, p, q)) continue;
            for (int16_t t : G.players[q].techs)
                if (kTechs[t].faction < 0 && !pl.hasTech(t) && techAvailableToResearch(G, p, t)) {
                    bool dup = false;
                    for (const Option& o : opts)
                        if (o.id == t) dup = true;
                    if (!dup) opts.push_back({t, kTechs[t].name, std::string("from ") + pn(q) + ". " + techSummary(t)});
                }
        }
        if (opts.empty()) return;
        int t = pick(p, Tag::CardTarget, "Plagiarize: gain which technology?", opts, "Costs 5 influence.");
        if (pay(p, 5, true, "Plagiarize", false)) {
            // (fixes 27, the Nekro review - owner 2026-11-14) "Gain that technology": a gain, not research (Anti-Intellectual
            // Revolution, Research Agreement)
            researchTech(p, t, G.fixes < 27);
            E.log(p, pn(p) + " gains " + kTechs[t].name + ".", EventKind::Research);
        }
        return;
    }
    if (is("plague")) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            int q = G.planets[x].owner;
            if (q < 0 || q == p || !G.planets[x].units[q].count(UnitType::Infantry)) continue;
            opts.push_back({int(x), planetName(int(x)) + " (" + pn(q) + ")",
                            std::to_string(G.planets[x].units[q].count(UnitType::Infantry)) + " infantry"});
        }
        int x = pick(p, Tag::CardTarget, "Plague: which planet?", opts, "", false, true);
        int q = G.planets[x].owner;
        RollResult r;
        for (int i = 0; i < G.planets[x].units[q].count(UnitType::Infantry); ++i) {
            int v = E.rng().d10();
            r.dice.push_back(int8_t(v));
            r.need.push_back(6);
            if (v >= 6) ++r.hits;
        }
        reportRoll(p, G.planets[x].system, "Plague on " + planetName(x), r);
        if (r.hits) destroyUnits(q, UnitType::Infantry, r.hits, G.planets[x].system, x, p, "Plague");
        return;
    }
    if (is("refit")) {
        std::vector<Option> opts;
        const int np = int(G.planets.size());
        for (int x = 0; x < np; ++x) {
            int n = G.planets[x].units[p].count(UnitType::Infantry);
            for (int i = 0; i < n && i < 2; ++i) opts.push_back({x * 4 + i, "Infantry on " + planetName(x), ""});
        }
        for (size_t sy = 0; sy < G.systems.size(); ++sy) {  // infantry in space count too (0.21.1 audit)
            int n = G.systems[sy].space[p].count(UnitType::Infantry);
            for (int i = 0; i < n && i < 2; ++i) opts.push_back({(np + int(sy)) * 4 + i, "Infantry in space, " + sysName(int(sy)), ""});
        }
        int maxN = std::min<int>(2, std::min<int>(int(opts.size()), unitsAvailable(G, p, UnitType::Mech)));
        std::vector<int> picks = pickMany(p, Tag::CardTarget, "Refit Troops: replace which infantry with mechs?", opts, 1, maxN);
        for (int id : picks) {
            int x = id / 4;
            Force& f = x >= np ? G.systems[x - np].space[p] : G.planets[x].units[p];
            f.remove(UnitType::Infantry, 1, false);
            f.add(UnitType::Mech, 1);
        }
        say(p, pn(p) + " refits " + std::to_string(picks.size()) + " infantry into mechs.");
        return;
    }
    if (is("repeal")) {
        std::vector<Option> opts;
        for (size_t i = 0; i < G.laws.size(); ++i)
            opts.push_back({int(i), kAgendas[G.laws[i].agenda].name, agendaText(G.laws[i].agenda, false)});
        int i = pick(p, Tag::CardTarget, "Repeal Law: discard which law?", opts);
        discardLaw(i, "Repeal Law");
        return;
    }
    if (is("scuttle")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            for (int t : kShipIdx) {
                if (t == int(UnitType::Fighter)) continue;
                for (int i = 0; i < G.systems[s].space[p].n[t] && i < 2; ++i)
                    opts.push_back({int(s) * 64 + t * 4 + i,
                                    std::string(unitTypeName(UnitType(t))) + " in " + sysName(int(s)),
                                    "worth " + std::to_string(costOf(G, p, UnitType(t))) + " trade goods"});
            }
        std::vector<int> picks = pickMany(p, Tag::CardTarget, "Scuttle: return which 1 or 2 ships?", opts, 1,
                                          std::min<int>(2, int(opts.size())));
        int gain = 0;
        for (int id : picks) {
            int s = id / 64, t = (id % 64) / 4;
            G.systems[s].space[p].remove(UnitType(t), 1, true);
            gain += costOf(G, p, UnitType(t));
            removeExcessCapacity(p, s);
        }
        gainTG(p, gain);
        say(p, pn(p) + " scuttles " + std::to_string(picks.size()) + " ships for " + std::to_string(gain) + " trade goods.");
        return;
    }
    if (is("spy")) {
        std::vector<Option> opts;
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].actionCards.empty())
                opts.push_back({q, pn(q), std::to_string(G.players[q].actionCards.size()) + " action cards"});
        int q = pick(p, Tag::CardTarget, "Spy: take a random action card from whom?", opts);
        std::vector<int16_t>& h = G.players[q].actionCards;
        int i = int(E.rng().below(uint32_t(h.size())));
        int taken = h[i];
        h.erase(h.begin() + i);
        pl.actionCards.push_back(int16_t(taken));
        E.revealed();
        say(p, pn(p) + " takes an action card from " + pn(q) + ".");
        enforceHandLimit(p);
        return;
    }
    if (is("tactical")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            bool bomb = false;
            for (int t = 0; t < kUnitTypes; ++t)
                if (G.systems[s].space[p].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) bomb = true;
            if (bomb) opts.push_back({int(s), sysName(int(s)), ""});
        }
        int s = pick(p, Tag::CardTarget, "Tactical Bombardment: which system?", opts, "", true);
        for (int16_t x : G.systems[s].planets)
            if (G.planets[x].owner >= 0 && G.planets[x].owner != p) G.planets[x].exhausted = true;
        say(p, pn(p) + " exhausts the enemy planets in " + sysName(s) + ".", s);
        return;
    }
    if (is("unexpected")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
        int s = pick(p, Tag::CardTarget, "Unexpected Action: remove your command token from which system?", opts, "", true);
        G.systems[s].tokens &= uint16_t(~(1u << p));
        say(p, pn(p) + " removes the command token from " + sysName(s) + ".", s);
        return;
    }
    if (is("unstable")) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x)
            if (hasTraitOf(G, int(x), Trait::Hazardous)) {
                int inf = 0;
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != p) inf += G.planets[x].units[q].count(UnitType::Infantry);
                opts.push_back({int(x), planetName(int(x)), std::to_string(inf) + " enemy infantry"});
            }
        int x = pick(p, Tag::CardTarget, "Unstable Planet: which hazardous planet?", opts, "", false, true);
        G.planets[x].exhausted = true;
        // "Up to 3 infantry": the player playing it never destroys their own (0.21.1 audit).
        int left = 3;
        for (int q : clockwiseFrom(G, (p + 1) % G.numPlayers)) {
            if (q == p || left <= 0) continue;
            int n = std::min(left, G.planets[x].units[q].count(UnitType::Infantry));
            if (n > 0) destroyUnits(q, UnitType::Infantry, n, G.planets[x].system, x, p, "Unstable Planet");
            left -= n;
        }
        return;
    }
    if (is("uprising")) {
        std::vector<Option> opts;
        for (size_t x = 0; x < G.planets.size(); ++x) {
            int q = G.planets[x].owner;
            if (q < 0 || q == p || G.planets[x].exhausted || G.systems[G.planets[x].system].home >= 0) continue;
            opts.push_back({int(x), planetName(int(x)) + " (" + pn(q) + ")", std::to_string(planetResources(G, int(x))) + " resources"});
        }
        int x = pick(p, Tag::CardTarget, "Uprising: exhaust which planet?", opts, "", false, true);
        G.planets[x].exhausted = true;
        gainTG(p, planetResources(G, x));
        say(p, pn(p) + " exhausts " + planetName(x) + " and gains " + std::to_string(planetResources(G, x)) + " trade goods.");
        return;
    }
    if (is("war_effort")) {
        std::vector<Option> opts;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (hasShips(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
        int s = pick(p, Tag::CardTarget, "War Effort: place a cruiser where?", opts, "", true);
        addUnits(p, UnitType::Cruiser, 1, s, -1);
        enforceFleetLimit(p);
        say(p, pn(p) + " places a cruiser in " + sysName(s) + ".", s);
        return;
    }
}

// ============================================================ component actions
bool Rules::componentCardUsable(int p, int card) const {
    const Player& pl = G.players[p];
    const char* k = kActionCards[card].key;
    auto is = [&](const char* x) { return !std::strcmp(k, x); };
    if (!actionCardImplemented(card) || !actionCardIsComponent(card)) return false;
    if (usCard(card)) return usComponentUsable(p, card);  // Uncharted Space (0.33.0)
    if (teCard(card)) return teComponentUsable(p, card);  // Thunder's Edge (0.34.0)
    if (is("arch_expedition")) {
        for (int x : controlledPlanets(G, p))
            for (int deck = 0; deck < 3; ++deck)
                if (hasTraitOf(G, x, deck == 0 ? Trait::Cultural : deck == 1 ? Trait::Hazardous : Trait::Industrial) &&
                    (!G.exploreDeck[deck].empty() || !G.exploreDiscard[deck].empty()))
                    return true;
        return false;
    }
    if (is("probe")) {
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (!G.systems[s].frontier) continue;
            if (hasShips(G, p, int(s))) return true;
            for (int16_t a : G.systems[s].adj)
                if (hasShips(G, p, a)) return true;
        }
        return false;
    }
    if (is("seize")) {
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].eliminated && fragmentCount(G, q) > 0 && areNeighbours(G, p, q)) return true;
        return false;
    }
    if (is("cripple")) {
        for (const PlanetState& x : G.planets)
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && x.units[q].count(UnitType::PDS)) return true;
        return false;
    }
    if (is("divert_funding")) {
        for (int16_t t : pl.techs)
            if (!kTechs[t].unitUpgrade && kTechs[t].faction < 0) return true;
        return false;
    }
    if (is("economic_initiative")) {
        for (int x : controlledPlanets(G, p))
            if (hasTraitOf(G, x, Trait::Cultural) && G.planets[x].exhausted) return true;
        return false;
    }
    if (is("f_conscription")) return true;
    if (is("f_deployment")) return !controlledPlanets(G, p).empty();
    if (is("f_researched")) {
        if (pl.tradeGoods < 4) return false;
        for (int t = 0; t < kTechCount; ++t)
            if (canResearchNow(G, p, t)) return true;
        return false;
    }
    if (is("ghost_ship")) {
        if (unitsAvailable(G, p, UnitType::Destroyer) <= 0) return false;
        for (size_t s = 0; s < G.systems.size(); ++s)
            if (G.systems[s].home < 0 && (wormholesOf(G, s) & (kAlpha | kBeta | kGamma | kDelta)) && !otherShipsIn(G, p, int(s)) &&
                !G.systems[s].offBoard)
                return true;
        return false;
    }
    if (is("impersonation")) return spendable(G, p, true) >= 3 && !G.secretDeck.empty();
    if (is("industrial_initiative")) {
        for (int x : controlledPlanets(G, p))
            if (hasTraitOf(G, x, Trait::Industrial)) return true;
        return false;
    }
    if (is("insub")) {
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && G.players[q].tactic > 0 && !G.players[q].eliminated) return true;
        return false;
    }
    if (is("jamming")) {
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (G.systems[s].home >= 0 || G.systems[s].isHyperlane()) continue;
            bool near = hasShips(G, p, int(s));
            for (int16_t a : G.systems[s].adj)
                if (hasShips(G, p, a)) near = true;
            if (!near) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && !G.players[q].eliminated && !G.systems[s].hasToken(q)) return true;
        }
        return false;
    }
    if (is("lucky")) {
        for (size_t s = 0; s < G.systems.size(); ++s) {
            bool mine = false;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].owner == p) mine = true;
            if (!mine) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p)
                    for (UnitType t : {UnitType::Dreadnought, UnitType::Cruiser, UnitType::Destroyer})
                        if (G.systems[s].space[q].count(t)) return true;
        }
        return false;
    }
    if (is("meltdown")) {
        for (const PlanetState& x : G.planets) {
            if (G.systems[x.system].home >= 0) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && x.units[q].count(UnitType::SpaceDock)) return true;
        }
        for (const SystemState& sy : G.systems) {
            if (sy.home >= 0) continue;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && sy.space[q].count(UnitType::SpaceDock)) return true;
        }
        return false;
    }
    if (is("messiah")) return !controlledPlanets(G, p).empty();
    if (is("mining_initiative")) return !controlledPlanets(G, p).empty();
    if (is("plagiarize")) {
        if (spendable(G, p, true) < 5) return false;
        for (int q = 0; q < G.numPlayers; ++q) {
            if (q == p || !areNeighbours(G, p, q)) continue;
            for (int16_t t : G.players[q].techs)
                if (kTechs[t].faction < 0 && !pl.hasTech(t) && techAvailableToResearch(G, p, t)) return true;
        }
        return false;
    }
    if (is("plague")) {
        for (const PlanetState& x : G.planets)
            if (x.owner >= 0 && x.owner != p && x.units[x.owner].count(UnitType::Infantry)) return true;
        return false;
    }
    if (is("refit")) {
        if (unitsAvailable(G, p, UnitType::Mech) <= 0) return false;
        for (const PlanetState& x : G.planets)
            if (x.units[p].count(UnitType::Infantry)) return true;
        for (const SystemState& sy : G.systems)
            if (sy.space[p].count(UnitType::Infantry)) return true;
        return false;
    }
    if (is("repeal")) return !G.laws.empty();
    if (is("scuttle")) {
        for (const SystemState& s : G.systems)
            if (s.space[p].nonFighterShips()) return true;
        return false;
    }
    if (is("spy")) {
        for (int q = 0; q < G.numPlayers; ++q)
            if (q != p && !G.players[q].actionCards.empty()) return true;
        return false;
    }
    if (is("tactical")) {
        for (const SystemState& s : G.systems)
            for (int t = 0; t < kUnitTypes; ++t)
                if (s.space[p].n[t] && unitDef(G, p, UnitType(t)).bombard > 0) return true;
        return false;
    }
    if (is("unexpected")) {
        for (const SystemState& s : G.systems)
            if (s.hasToken(p)) return true;
        return false;
    }
    if (is("unstable")) {
        for (size_t x = 0; x < G.planets.size(); ++x)
            if (hasTraitOf(G, int(x), Trait::Hazardous)) return true;
        return false;
    }
    if (is("uprising")) {
        for (const PlanetState& x : G.planets)
            if (x.owner >= 0 && x.owner != p && !x.exhausted && G.systems[x.system].home < 0) return true;
        return false;
    }
    if (is("war_effort")) {
        if (unitsAvailable(G, p, UnitType::Cruiser) <= 0) return false;
        for (const SystemState& s : G.systems)
            if (s.space[p].ships()) return true;
        return false;
    }
    return false;
}

std::vector<Option> Rules::componentActionOptions(int p) {
    std::vector<Option> opts;
    const Player& pl = G.players[p];
    std::vector<int16_t> hand = pl.actionCards;
    for (int16_t s : salvageOf(p)) hand.push_back(s);  // Dok 'N Pic's Salvage Yard (Thunder's Edge)
    if (canPlayActionCards(G, p))
        for (int16_t c : hand) {
            bool dup = false;
            for (const Option& o : opts)
                if (o.id == 1000 + c) dup = true;
            if (dup || !componentCardUsable(p, c)) continue;
            opts.push_back({1000 + c, std::string("Action card: ") + kActionCards[c].name, actionCardText(c)});
        }
    // Technologies with a component action
    if (techReady(p, "sr")) {
        bool dock = false;
        for (size_t s = 0; s < G.systems.size(); ++s) {
            if (blockaded(G, p, int(s))) continue;
            if (G.fixes >= 11) {  // (0.44.7: only where a ship can really be made - LRR 22.3)
                dock = dock || slingRelayCan(p, int(s));
                continue;
            }
            if (G.systems[s].space[p].count(UnitType::SpaceDock)) dock = true;
            for (int16_t x : G.systems[s].planets)
                if (G.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
        }
        if (dock) opts.push_back({2000 + findTech("sr"), "Technology: Sling Relay", techSummary(findTech("sr"))});
    }
    // Relic fragments: purge 3 of a kind for a relic (unknown fragments count as any kind).
    if (!G.relicDeck.empty()) {
        const int need = eventOn(G, kEvExploration) ? 2 : 3;  // Age of Exploration (galactic event): 2
        for (int t = 0; t < 3; ++t)
            if (relicFragments(G, p, t) >= need)
                opts.push_back({3000 + t, std::string("Relic fragments: purge ") + std::to_string(need) + " " + exploreTypeName(ExploreType(t)),
                                "Gain a relic."});
    }
    // Relics with an action
    for (int16_t r : pl.relics) {
        const char* k = kRelics[r].key;
        if (!std::strcmp(k, "codex") && !G.actionDiscard.empty())
            opts.push_back({4000 + r, "Relic: The Codex", relicText(r)});
        if (!std::strcmp(k, "stellarconverter")) opts.push_back({4000 + r, "Relic: Stellar Converter", relicText(r)});
        // Uncharted Space (0.33.0)
        if (!std::strcmp(k, "decrypted_cartoglyph") && !blueTilesLeft(G).empty() && !boardEdgeSpots(G).empty())
            opts.push_back({4000 + r, "Relic: Azdel's Key", relicText(r)});
        if (!std::strcmp(k, "throne_of_the_false_emperor") && (!G.relicDeck.empty() || !throneSecrets(p).empty()))
            opts.push_back({4000 + r, "Relic: Forgotten Throne", relicText(r)});
    }
    teRelicOptions(p, opts);  // Thunder's Edge relics (0.34.0)
    btOptions(p, opts);       // Thunder's Edge breakthroughs (0.34.0)
    // Star Charts (Uncharted Space): purge 2 for a new system at the edge of the board
    if (starChartCount(G, p) >= 2 && !blueTilesLeft(G).empty() && !boardEdgeSpots(G).empty())
        opts.push_back({3010, "Star Charts: purge 2", "Draw a random blue-backed system tile and place it at the edge of the board."});
    if (pl.enigmatic > 0 && spendable(G, p, false) >= 6)
        opts.push_back({5000, "Enigmatic Device", "Spend 6 resources to research a technology."});
    return opts;
}

bool Rules::componentAction(int p, int option) {
    if (option >= 6000 + int(Ab::BtArborec) && option <= 6000 + int(Ab::BtYssaril)) return btAction(p, option - 6000);
    if (option >= 6000 + int(Ab::DsbtAxis) && option <= 6000 + int(Ab::DsbtKyro)) return dsbtAction(p, option - 6000);  // (0.43.0)
    if (option >= 6000) return factionAction(p, option);
    if (option >= 5000) return enigmaticAction(p);
    if (option >= 4000) return relicAction(p, option - 4000);
    if (option == 3010) return starChartAction(p);
    if (option >= 3000) return fragmentAction(p, option - 3000);
    if (option >= 2000) {
        int t = option - 2000;
        if (!std::strcmp(kTechs[t].key, "sr")) return slingRelay(p);
        return false;
    }
    int card = option - 1000;
    if (!playActionCard(p, card, nullptr)) {
        // LRR 22.4: a cancelled component action doesn't use the action.
        return false;
    }
    suffiAnUnlock(p, card);  // Keleres commander: 1 trade good after a component action card
    return true;
}

// ============================================================ technologies
// Sling Relay (0.44.7, fixes 11): a system with a space dock of ours (the Ghoti All Mother counts) where 1 ship fits
// and can be paid for - a component action must be able to resolve (LRR 22.3)
bool Rules::slingRelayCan(int p, int s) const {
    if (blockaded(G, p, s)) return false;
    bool dock = G.systems[size_t(s)].space[size_t(p)].count(UnitType::SpaceDock) > 0;
    for (int16_t x : G.systems[size_t(s)].planets)
        if (G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock)) dock = true;
    if ((G.modules & kModDiscordant) && fac(p, "ghoti") && flagshipAbility(G, p, "ghoti") && G.systems[size_t(s)].space[size_t(p)].count(UnitType::Flagship))
        dock = true;
    if (!dock) return false;
    int mask = 0;
    for (int t : kShipIdx) mask |= 1 << t;
    for (int t : kShipIdx) {
        if (unitsAvailable(G, p, UnitType(t)) <= 0 || unitCost2(G, p, UnitType(t)) <= 0) continue;
        std::vector<int> one(kUnitTypes + 1, 0);
        one[size_t(t)] = 1;
        one[kUnitTypes] = -1;
        if (!checkProducePlan(G, p, s, one, 1, mask).empty()) continue;
        int c[kUnitTypes] = {};
        c[t] = 1;
        if (productionCost(G, p, c) <= spendable(G, p, false)) return true;
    }
    return false;
}

bool Rules::slingRelay(int p) {
    const bool v11 = G.fixes >= 11;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s) {
        if (v11) {
            if (slingRelayCan(p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
            continue;
        }
        bool dock = G.systems[s].space[p].count(UnitType::SpaceDock) > 0;
        for (int16_t x : G.systems[s].planets)
            if (G.planets[x].units[p].count(UnitType::SpaceDock)) dock = true;
        if (dock && !blockaded(G, p, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
    }
    if (opts.empty()) return false;
    opts.push_back({-1, "Cancel", ""});
    curCard_ = -1;
    int s = pick(p, Tag::ComponentChoice, "Sling Relay: produce 1 ship in which system?", opts, "", true);
    if (s < 0) return false;
    exhaustTech(p, "sr");
    say(p, pn(p) + " uses Sling Relay.", s);
    const int before = G.systems[size_t(s)].space[size_t(p)].ships();
    notProductionAbility_ = true;
    production(p, s, 1, false, true);
    notProductionAbility_ = false;
    if (v11 && G.systems[size_t(s)].space[size_t(p)].ships() <= before) {  // nothing built: as if not used (LRR 22.4)
        auto& ex = G.players[size_t(p)].exhaustedTechs;
        const int t = findTech("sr");
        ex.erase(std::remove(ex.begin(), ex.end(), int16_t(t)), ex.end());
        say(p, pn(p) + " builds nothing - Sling Relay is ready again.", s);
        return false;
    }
    return true;
}

void Rules::startOfTurn(int p) {
    startOfTurnFactions(p);  // Military Support, Infantry II coming back
    // Transit Diodes: move up to 4 of your ground forces onto planets you control.
    if (!techReady(p, "td")) return;
    if (G.fixes >= 9) {
        transitDiodes(p);
        return;
    }
    std::vector<int> mine = controlledPlanets(G, p);
    if (mine.size() < 2) return;
    std::vector<Option> units;
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const Force& f = G.planets[x].units[p];
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            for (int i = 0; i < f.count(t) && i < 4; ++i)
                units.push_back({int(x) * 64 + int(t) * 4 + i, std::string(unitTypeName(t)) + " on " + planetName(int(x)), ""});
    }
    if (units.empty()) return;
    if (!yesNo(p, Tag::UseTech, "Exhaust Transit Diodes?", "Move up to 4 of your ground forces onto a planet you control.")) return;
    std::vector<int> picks = pickMany(p, Tag::ComponentChoice, "Transit Diodes: choose up to 4 ground forces", units, 1,
                                      std::min<int>(4, int(units.size())));
    std::vector<Option> dest;
    for (int x : mine) dest.push_back({x, planetName(x), ""});
    int to = pick(p, Tag::ComponentChoice, "Transit Diodes: move them to which planet?", dest, "", false, true);
    exhaustTech(p, "td");
    for (int id : picks) {
        int x = id / 64, t = (id % 64) / 4;
        G.planets[x].units[p].remove(UnitType(t), 1, false);
        G.planets[to].units[p].add(UnitType(t), 1);
    }
    say(p, pn(p) + " uses Transit Diodes: " + std::to_string(picks.size()) + " ground forces move to " + planetName(to) + ".",
        G.planets[to].system);
}

// Transit Diodes (0.44.5, fixes 9): up to 4 ground forces from anywhere on the board - planets or space areas - onto 1
// or more planets you control (the old version took them from planets only, onto one planet, and needed 2 planets)
void Rules::transitDiodes(int p) {
    std::vector<int> mine = controlledPlanets(G, p);
    if (mine.empty()) return;
    std::vector<Option> units;
    // (0.44.9, fixes 13 - the owner's answer C9: the Titans' Hel-Titans (PDS that are ground forces) can go too)
    const bool hel = G.fixes >= 13 && fac(p, "titans");
    for (size_t x = 0; x < G.planets.size(); ++x) {
        const Force& f = G.planets[x].units[size_t(p)];
        if (f.groundForces() == 0 && !(hel && f.count(UnitType::PDS))) continue;
        bool elsewhere = false;  // (another planet of ours to go to)
        for (int y : mine) elsewhere = elsewhere || y != int(x);
        if (!elsewhere) continue;
        for (UnitType t : {UnitType::Infantry, UnitType::Mech, UnitType::PDS}) {
            if (t == UnitType::PDS && !hel) continue;
            for (int i = 0; i < f.count(t) && i < 4; ++i)
                units.push_back({int(x) * 64 + int(t) * 4 + i, std::string(t == UnitType::PDS ? "Hel-Titan" : unitTypeName(t)) + " on " + planetName(int(x)), ""});
        }
    }
    for (size_t s = 0; s < G.systems.size(); ++s) {
        const Force& f = G.systems[s].space[size_t(p)];
        for (UnitType t : {UnitType::Infantry, UnitType::Mech})
            for (int i = 0; i < f.count(t) && i < 4; ++i)
                units.push_back({kTdSpace + int(s) * 64 + int(t) * 4 + i, std::string(unitTypeName(t)) + " in space, " + sysName(int(s)), ""});
    }
    if (units.empty()) return;
    if (!yesNo(p, Tag::UseTech, "Exhaust Transit Diodes?", "Move up to 4 of your ground forces (from planets or space) onto planets you control."))
        return;
    std::vector<int> left = pickMany(p, Tag::ComponentChoice, "Transit Diodes: choose up to 4 ground forces", units, 1,
                                     std::min<int>(4, int(units.size())));
    exhaustTech(p, "td");
    std::vector<Option> dest;
    for (int x : mine) dest.push_back({x, planetName(x), ""});
    while (!left.empty()) {
        const int to = dest.size() == 1 ? dest[0].id
                                         : pick(p, Tag::ComponentChoice, left.size() > 1 ? "Transit Diodes: move them to which planet?"
                                                                                         : "Transit Diodes: move it to which planet?",
                                                dest, "", false, true);
        std::vector<int> go = left;
        if (left.size() > 1 && dest.size() > 1) {  // several planets: which of them go there (the rest is asked next)
            std::vector<Option> o;
            for (int id : left)
                for (const Option& u : units)
                    if (u.id == id) o.push_back(u);
            go = pickMany(p, Tag::ComponentChoice, "Transit Diodes: which of them go to " + planetName(to) + "?", o, 1, int(o.size()));
        }
        for (int id : go) {
            const int t = (id % 64) / 4;
            if (t == int(UnitType::PDS) && G.planets[size_t(to)].units[size_t(p)].count(UnitType::PDS) >= 2 && !lawInPlay(G, "defense_act")) {
                say(p, "A Hel-Titan can't join 2 others on " + planetName(to) + " - it stays where it is.", G.planets[size_t(to)].system);
                left.erase(std::find(left.begin(), left.end(), id));
                continue;
            }
            if (id >= kTdSpace) {
                const int s = (id - kTdSpace) / 64;
                G.systems[size_t(s)].space[size_t(p)].remove(UnitType(t), 1, false);
            } else {
                G.planets[size_t(id / 64)].units[size_t(p)].remove(UnitType(t), 1, false);
            }
            G.planets[size_t(to)].units[size_t(p)].add(UnitType(t), 1);
            left.erase(std::find(left.begin(), left.end(), id));
        }
        say(p, pn(p) + " uses Transit Diodes: " + std::to_string(go.size()) + " ground force" + (go.size() == 1 ? "" : "s") + " move to " +
                   planetName(to) + ".",
            G.planets[size_t(to)].system);
    }
}

void Rules::bioStims(int p) {
    if (!techReady(p, "bs")) return;
    std::vector<Option> opts;
    for (int x : controlledPlanets(G, p))
        if (G.planets[x].exhausted && planetHasSkip(G, x)) opts.push_back({x, "Ready " + planetName(x), "technology specialty planet"});
    for (int16_t t : G.players[p].exhaustedTechs)
        if (std::strcmp(kTechs[t].key, "bs") != 0) opts.push_back({10000 + t, std::string("Ready ") + kTechs[t].name, techSummary(t)});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't use Bio-Stims", ""});
    int c = pick(p, Tag::UseTech, "Exhaust Bio-Stims at the end of your turn?", opts);
    if (c < 0) return;
    exhaustTech(p, "bs");
    if (c >= 10000) {
        auto& ex = G.players[p].exhaustedTechs;
        ex.erase(std::find(ex.begin(), ex.end(), int16_t(c - 10000)));
        if (G.fixes >= 10) say(p, pn(p) + " readies " + kTechs[c - 10000].name + " (Bio-Stims).");  // (0.44.6: a feed line)
    } else {
        G.planets[c].exhausted = false;
        if (G.fixes >= 10) say(p, pn(p) + " readies " + planetName(c) + " (Bio-Stims).", G.planets[size_t(c)].system);
    }
}

void Rules::endOfTurn(int p) {
    if (G.fixes >= 27) floatingSweep();  // (fixes 27, the Saar review) blockaded floating factories
    if (G.players[p].eliminated) return;
    promiseEndOfTurn(p);  // "leave this system" promises
    persEndOfTurn(p);
    kingslayEndOfTurn(p);  // 0.19.0: Mecatol Rex held too long
    if (G.fixes < 10) bioStims(p);
    legendaryAbilities(p);
    teEndOfTurn(p);  // Thunder's Edge: The Acropolis, The Galactic Council
    if (G.modules & kModDiscordant) dsTurnEnd(p);  // Emergency Mobilization, Operator Kkavras (Discordant Stars)
    endOfTurn4(p);  // Garv and Gunn, Field Marshal Mercer
    // Predictive Intelligence: rearrange command tokens.
    if (techReady(p, "pi") &&
        yesNo(p, Tag::UseTech, "Exhaust Predictive Intelligence to rearrange your command tokens?", "")) {
        exhaustTech(p, "pi");
        gainTokens(p, 0, true, "Predictive Intelligence: rearrange your command tokens.");
    }
    // Bio-Stims (0.44.6, fixes 10): last, so it can ready what the end of the turn exhausted (Predictive Intelligence)
    if (G.fixes >= 10) bioStims(p);
}

}  // namespace ti
