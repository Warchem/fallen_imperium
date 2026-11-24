// Absol's Agendas (0.81.0, owner 2026-11-21) - Absol197's 60-card agenda deck, a module (kModAbsol) that REPLACES the
// normal agenda deck. The cards' own wording is in src/engine/gen/absol_text.inc (tools/gen_absol_text.py).
//
// What the deck shares (built once here):
//  - "the player who cast the most votes For / Against / for the elected ..." (absolTop): ties are decided by the speaker
//    (owner: "the rules don't change, the speaker will decide the outcome if a tie was to occur"); Committee Formation's
//    owner counts as that player for the outcome it chose; "the other player who cast the most votes" leaves one out;
//  - "players who voted Against or abstained" (absolAbstained: asked to vote and cast none - Absol 8.14b: not unable players);
//  - "give 1 promissory note from their hand, if able" (absolGiveNote: the giver's choice of note and, where the card
//    allows several, of the player);
//  - control tokens on a law card (Law::tokens);
//  - "Exhaust the <X> strategy card" (absolLockCard): owner - nobody can pick it in the next strategy phase;
//  - "reveal cards from the bottom of the agenda deck until a law is revealed" (absolRevealFromBottom);
//  - Emergency Session: not voted on - one more agenda once the others are resolved (absolRevealSkip, agendaPhase).
// Everything here runs only in games with the module: without it no Absol card is in the deck, and every hook returns
// at once - older saves replay exactly as before.
#include <algorithm>
#include <cstring>

#include "engine/map.h"
#include "engine/rules.h"

namespace ti {

namespace {
bool abKey(int a, const char* k) { return a >= 0 && a < kAgendaCount && !std::strcmp(kAgendas[a].key, k); }
const Law* lawByKey(const Game& g, const char* key) {
    if (g.lawsBlank) return nullptr;
    if (g.monLawsBlankFor >= 0 && g.monLawsBlankFor == g.activePlayer) return nullptr;  // (0.82.4) Verdant Halo
    for (const Law& l : g.laws)
        if (!std::strcmp(kAgendas[l.agenda].key, key)) return &l;
    return nullptr;
}
Law* lawByKeyMut(Game& g, const char* key) {
    for (Law& l : g.laws)
        if (!std::strcmp(kAgendas[l.agenda].key, key)) return &l;
    return nullptr;
}
int sumPrereqs(int t) { return kTechs[t].req[0] + kTechs[t].req[1] + kTechs[t].req[2] + kTechs[t].req[3]; }
bool explorable(const Game& g, int x) {
    return hasTraitOf(g, x, Trait::Cultural) || hasTraitOf(g, x, Trait::Hazardous) || hasTraitOf(g, x, Trait::Industrial);
}
}  // namespace

// ------------------------------------------------------------------ the shared pieces
int Rules::absolTop(int a, int o, const std::vector<int>& votedFor, const std::vector<int>& votes, int exclude) {
    if (absolTopOverride_ >= 0 && absolTopOverride_ != exclude && !G.players[size_t(absolTopOverride_)].eliminated)
        return absolTopOverride_;
    int best = 0;
    std::vector<int> tied;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == exclude || G.players[size_t(q)].eliminated || q >= int(votedFor.size()) || votedFor[size_t(q)] != o) continue;
        const int v = q < int(votes.size()) ? votes[size_t(q)] : 0;
        if (v <= 0) continue;
        if (v > best) {
            best = v;
            tied.assign(1, q);
        } else if (v == best) {
            tied.push_back(q);
        }
    }
    // Absol rulebook 8.14c / 8.18b and the FAQ: "If no one voted for the resolved outcome, the speaker is considered to be the person
    // who cast the most votes for the resolved outcome"; an action asked of the top voter of an outcome nobody voted for - the speaker
    if (tied.empty())
        return G.speaker >= 0 && G.speaker != exclude && !G.players[size_t(G.speaker)].eliminated ? int(G.speaker) : -1;
    if (tied.size() == 1) return tied[0];
    // (owner 2026-11-21) a tie: the speaker decides
    std::vector<Option> opts;
    for (int q : tied) opts.push_back({q, pn(q), std::to_string(best) + " votes"});
    const int prevA = curAgenda_, prevV = curValue_;
    curAgenda_ = a;
    curValue_ = o;
    const int w = pick(G.speaker, Tag::AbsolTie,
                       std::string(kAgendas[a].name) + ": tied for the most votes - you are the speaker, choose who cast the most",
                       opts, "The speaker breaks the tie.");
    curAgenda_ = prevA;
    curValue_ = prevV;
    say(G.speaker, pn(G.speaker) + " (speaker) decides that " + pn(w) + " cast the most votes.");
    return w;
}

std::vector<int> Rules::absolVotersByMost(int o, const std::vector<int>& votedFor, const std::vector<int>& votes) const {
    std::vector<int> order = clockwiseFrom(G, (G.speaker + 1) % G.numPlayers), v;
    for (int q : order)
        if (q < int(votedFor.size()) && votedFor[size_t(q)] == o && q < int(votes.size()) && votes[size_t(q)] > 0 &&
            !G.players[size_t(q)].eliminated)
            v.push_back(q);
    std::stable_sort(v.begin(), v.end(), [&](int x, int y) { return votes[size_t(x)] > votes[size_t(y)]; });
    return v;
}

bool Rules::absolAbstained(int q, const std::vector<int>& votedFor, const std::vector<int>& votes) const {
    if (q < 0 || q >= G.numPlayers || G.players[size_t(q)].eliminated) return false;
    // Absol rulebook 8.14b (and the FAQ): "If a player has no votes to cast, or cannot vote due to a game effect, they are not
    // considered to have abstained from voting" - only a player who was asked to vote and cast none abstained
    if (!((absolAbleCur_ >> q) & 1)) return false;
    return q >= int(votedFor.size()) || votedFor[size_t(q)] == kNoVote || q >= int(votes.size()) || votes[size_t(q)] <= 0;
}

bool Rules::absolVoting(int q, const std::vector<int>& votedFor) const {
    if (q < 0 || q >= G.numPlayers || G.players[size_t(q)].eliminated) return false;
    return ((absolAbleCur_ >> q) & 1) || (q < int(votedFor.size()) && votedFor[size_t(q)] != kNoVote);
}

bool Rules::absolGiveNote(int p, int to, const std::string& why) {
    if (p < 0 || to < 0 || p == to || G.players[size_t(p)].eliminated || G.players[size_t(to)].eliminated) return false;
    const std::vector<int16_t>& h = G.players[size_t(p)].notes;
    if (h.empty()) return false;
    std::vector<Option> opts;
    for (int16_t n : h) opts.push_back({n, noteNameOf(G, n), noteOwner(n) == to ? "Their own note: it goes back to them." : ""});
    const int prevF = curFor_;
    curFor_ = to;
    const int n = opts.size() == 1 ? opts[0].id
                                   : pick(p, Tag::AbsolNote, why + ": give " + pn(to) + " which promissory note?", opts,
                                          "From your hand.");
    curFor_ = prevF;
    giveNote(p, to, n);
    return true;
}

bool Rules::absolGiveNoteToOneOf(int p, const std::vector<int>& to, const std::string& why) {
    std::vector<Option> opts;
    for (int q : to)
        if (q != p && q >= 0 && !G.players[size_t(q)].eliminated) opts.push_back({q, pn(q), ""});
    if (opts.empty() || G.players[size_t(p)].notes.empty()) return false;
    const int q = opts.size() == 1 ? opts[0].id : pick(p, Tag::AbsolGiveTo, why + ": give a promissory note to whom?", opts);
    return absolGiveNote(p, q, why);
}

// "Exhaust the <X> strategy card" (7 cards). OWNER 2026-11-21 (RULES-CHECK 0.81.0 X1, kept after reading Absol's rulebook):
// nobody can choose an exhausted card in the next strategy phase (simpler for the bots); if every card left is exhausted, one may
// still be chosen (rules_round.cpp, absolCardLocked's second pass).
// ABSOL'S OWN RULING, in case we revisit it (docs/owner/absol/rulebook_faq_v4.txt, 84.1d / 84.2b / 81.6a and the FAQ): "Players
// may select an exhausted strategy card. An exhausted strategy card still provides its initiative value, but the player will not be
// able to use a strategic action with that strategy card while it is exhausted." No trade good goes on an unchosen exhausted card
// (done), and exhausted cards ready in the status phase. Switching would mean: drop the pick filter, block the strategic action
// (and its secondary) for a card in G.scLocked until the status phase, and teach the bots to value such a card for its number only.
void Rules::absolLockCard(int card, const std::string& why) {
    if (card < 0 || card >= kStrategyCards) return;
    G.scLocked = uint8_t(G.scLocked | (1u << card));
    say(-1, std::string("The ") + strategyName(card) + " strategy card is exhausted (" + why +
                "): nobody can choose it in the next strategy phase.");
    // Absol rulebook 8.23: "the player removes any trade goods tokens from that strategy card and returns them to the supply"
    if (G.cardTradeGoods[card] > 0) {
        say(-1, std::to_string(G.cardTradeGoods[card]) + " trade good(s) on " + strategyName(card) + " return to the supply.");
        G.cardTradeGoods[card] = 0;
    }
}

bool Rules::absolCanBeSpeaker(int q) const {
    if (!absolOn() || q < 0) return true;
    if (const Law* l = lawByKey(G, "absol_censure"))
        if (l->owner == q) return false;  // Political Censure: "cannot take or receive the speaker token"
    if (const Law* l = lawByKey(G, "absol_limits"))
        if (l->tokens[size_t(q)] > 0) return false;  // Term Limits
    return true;
}

void Rules::absolSpeakerTaken(int q) {
    if (!absolOn() || q < 0) return;
    Law* l = lawByKeyMut(G, "absol_limits");
    if (!l || G.lawsBlank) return;
    l->tokens[size_t(q)] = 1;
    say(q, pn(q) + " places a control token on Term Limits.");
    bool all = true;
    for (int r = 0; r < G.numPlayers; ++r)
        if (!G.players[size_t(r)].eliminated && !l->tokens[size_t(r)]) all = false;
    if (all) {
        for (uint8_t& t : l->tokens) t = 0;
        say(-1, "Every player has a control token on Term Limits: they are removed.");
    }
}

void Rules::absolSetSpeaker(int q, const std::string& why) {
    if (q < 0 || G.players[size_t(q)].eliminated) return;
    if (!absolCanBeSpeaker(q)) {
        say(q, pn(q) + " can't take the speaker token (" + why + ": Political Censure / Term Limits).");
        return;
    }
    G.speaker = int8_t(q);
    say(q, pn(q) + " takes the speaker token (" + why + ").");
    absolSpeakerTaken(q);
}

bool Rules::absolRevealSkip(int a) {
    if (a < 0 || kAgendas[a].elect != Elect::None) return false;
    // Emergency Session: "place it in the common play area. This card is not an agenda, and players do not vote on this card"
    emergencyCards_.push_back(int16_t(a));
    G.emergency = int8_t(G.emergency + 1);
    say(-1, "Emergency Session: one more agenda will be voted on once the others are resolved.");
    return true;
}

int Rules::absolRevealFromBottom() {
    std::vector<int16_t> seen;
    int law = -1;
    for (int guard = 0; guard < 80 && law < 0; ++guard) {
        if (G.agendaDeck.empty()) {
            if (G.agendaDiscard.empty()) break;
            G.agendaDeck = G.agendaDiscard;
            G.agendaDiscard.clear();
            E.rng().shuffle(G.agendaDeck);
        }
        const int a = G.agendaDeck.front();  // (the bottom of the deck: top = back())
        G.agendaDeck.erase(G.agendaDeck.begin());
        E.revealed();
        say(-1, std::string("From the bottom of the agenda deck: ") + kAgendas[a].name + ".");
        if (kAgendas[a].law) law = a;
        else seen.push_back(int16_t(a));
    }
    for (int16_t x : seen) G.agendaDeck.push_back(x);  // "shuffle the other revealed agendas back into the agenda deck"
    if (!seen.empty()) E.rng().shuffle(G.agendaDeck);
    return law;
}

// ------------------------------------------------------------------ resolving (from resolveOutcome)
void Rules::absolResolve(int a, int o, const std::vector<int>& votedFor, const std::vector<int>& votes) {
    const int prev = curAgenda_;
    const uint16_t prevAble = absolAbleCur_;  // (an outcome can hold another vote: Constitution, Miscount Disclosed)
    absolAbleCur_ = absolAble_;
    curAgenda_ = a;
    if (kAgendas[a].law) absolLawOutcome(a, o, votedFor, votes);
    else absolDirectiveOutcome(a, o, votedFor, votes);
    curAgenda_ = prev;
    absolAbleCur_ = prevAble;
}

void Rules::absolLawOutcome(int a, int o, const std::vector<int>& votedFor, const std::vector<int>& votes) {
    const int n = G.numPlayers;
    auto is = [&](const char* k) { return abKey(a, k); };
    const Elect el = kAgendas[a].elect;
    const bool against = el == Elect::ForAgainst && o == 1;
    auto top = [&](int out) { return absolTop(a, out, votedFor, votes); };
    auto voters = [&](int out) { return absolVotersByMost(out, votedFor, votes); };
    auto againstOrAbstained = [&]() {
        std::vector<int> v;
        for (int q = 0; q < n; ++q)
            if ((q < int(votedFor.size()) && votedFor[size_t(q)] == 1 && votes[size_t(q)] > 0) || absolAbstained(q, votedFor, votes))
                v.push_back(q);
        return v;
    };
    auto forOrAbstained = [&]() {
        std::vector<int> v;
        for (int q = 0; q < n; ++q)
            if ((q < int(votedFor.size()) && votedFor[size_t(q)] == 0 && votes[size_t(q)] > 0) || absolAbstained(q, votedFor, votes))
                v.push_back(q);
        return v;
    };
    auto placeTokens = [&](int p, int k) {
        if (p < 0) return;
        Law* l = lawByKeyMut(G, kAgendas[a].key);
        if (!l) return;
        l->tokens[size_t(p)] = uint8_t(l->tokens[size_t(p)] + k);
        say(p, pn(p) + " places " + std::to_string(k) + " control token" + (k > 1 ? "s" : "") + " on " + kAgendas[a].name + ".");
    };
    auto mostTrait = [&](int exclude, Trait t) {  // the other players with the most planets of a trait
        int best = -1;
        std::vector<int> v;
        for (int q = 0; q < n; ++q) {
            if (q == exclude || G.players[size_t(q)].eliminated) continue;
            int k = 0;
            for (int x : controlledPlanets(G, q))
                if (hasTraitOf(G, x, t)) ++k;
            if (k > best) {
                best = k;
                v.assign(1, q);
            } else if (k == best) {
                v.push_back(q);
            }
        }
        return v;
    };
    auto traitCount = [&](int p, Trait t) {
        int k = 0;
        for (int x : controlledPlanets(G, p))
            if (hasTraitOf(G, x, t)) ++k;
        return k;
    };

    // ---------------- elected laws
    if (el == Elect::Player) {
        addLaw(a, o, -1);
        if (is("absol_arbiter") || is("absol_formation")) {
            // "gives each other player who voted for them 1 promissory note from their hand (from most votes to least), if able"
            for (int q : voters(o))
                if (q != o) absolGiveNote(o, q, kAgendas[a].name);
        } else if (is("absol_censure")) {
            gainVP(o, 1, "Political Censure");
            if (G.speaker == o) {
                const int t = absolTop(a, o, votedFor, votes, o);
                if (t >= 0) {
                    G.speaker = int8_t(t);
                    say(t, pn(t) + " takes the speaker token (Political Censure).");
                    absolSpeakerTaken(t);
                }
            }
        } else if (is("absol_warrant")) {
            const int t = absolTop(a, o, votedFor, votes, o);  // "the non-elected player who cast the most votes for the elected player"
            placeTokens(t, 1);
            drawSecret(o);
            drawSecret(o);
        }
        return;
    }
    if (el == Elect::ScoredSecret) {  // Classified Document Leak
        G.leaked.push_back(int16_t(o));
        addLaw(a, -1, o);
        G.publicObjectives.push_back(int16_t(o));
        say(-1, std::string(kObjectives[o].name) + " becomes a public objective.");
        const int t = top(o);
        if (t >= 0) {
            if (!hasScored(G, t, o) && meetsObjective(G, t, o) && scoreObjective(t, o)) {
                // (scored)
            } else {
                drawActionCards(t, 2, "Classified Document Leak");
            }
        }
        return;
    }
    if (el == Elect::UnitUpgrade) {  // Publicize Weapon Schematics
        addLaw(a, -1, o);
        const int t = top(o);
        const int type = upgradeUnitType(o);
        if (t >= 0 && type >= 0) {
            std::vector<Option> opts;
            for (int x = 0; x < kTechCount; ++x)
                if (kTechs[x].unitUpgrade && upgradeUnitType(x) == type && !G.players[size_t(t)].hasTech(x) && techAvailableToResearch(G, t, x) &&
                    gainableTech(t, x))
                    opts.push_back({x, kTechs[x].name, techSummary(x)});
            if (!opts.empty()) {
                opts.push_back({-1, "Don't gain one", ""});
                curValue_ = kAbSchematicsGain;
                const int x = pick(t, Tag::AbsolChoice, "Publicize Weapon Schematics: gain a unit upgrade technology for that unit type?", opts);
                curValue_ = 0;
                if (x >= 0) {
                    researchTech(t, x, false);
                    E.log(t, pn(t) + " gains " + kTechs[x].name + ".", EventKind::Research);
                }
            }
        }
        return;
    }

    // ---------------- For / Against laws
    if (!against) {
        addLaw(a, -1, -1);
        const int t = top(0);
        if (is("absol_articleswar")) {
            placeTokens(t, 3);
        } else if (is("absol_checks") || is("absol_regulations") || is("absol_sanctions") || is("absol_recon")) {
            placeTokens(t, 1);
            if (is("absol_regulations"))
                for (int q = 0; q < n; ++q) {
                    Player& pl = G.players[size_t(q)];
                    const int cap = fleetPoolCap(G, q);
                    if (pl.fleet > cap) {
                        say(q, pn(q) + " returns " + std::to_string(pl.fleet - cap) + " fleet pool token(s) (Fleet Regulations).");
                        pl.fleet = int8_t(cap);
                    }
                    enforceFleetLimit(q);
                }
            if (is("absol_sanctions"))
                for (int q = 0; q < n; ++q) enforceHandLimit(q);
        } else if (is("absol_travelban")) {
            placeTokens(t, 3);
        } else if (is("absol_limits")) {
            placeTokens(G.speaker, 1);  // "The speaker places 1 of their control tokens on this card"
        } else if (is("absol_conscription")) {
            if (t >= 0) gainTG(t, traitCount(t, Trait::Industrial));
        } else if (is("absol_conventionswar")) {
            if (t >= 0) gainTG(t, traitCount(t, Trait::Cultural));
        } else if (is("absol_defenseact")) {
            if (t >= 0 && unitsForPlacement(G, t, UnitType::PDS) > 0)
                placeStructure(t, -1, false, true, true, "Homeland Defense Act: place a PDS on a planet you control?");
        } else if (is("absol_equality")) {
            for (Player& pl : G.players)
                if (!pl.eliminated) pl.tradeGoods = 0;
            say(-1, "Every player returns their trade goods; every commodity value is now 3.");
            if (t >= 0) {
                Player& pl = G.players[size_t(t)];
                const int before = pl.commodities;
                pl.commodities = int8_t(std::max<int>(pl.commodities, commodityCap(G, pl)));
                say(t, pn(t) + " replenishes their commodities.");
                replenished(t, pl.commodities - before);
            }
        } else if (is("absol_government")) {
            if (t >= 0) absolSetSpeaker(t, "Representative Government");
        } else if (is("absol_hyperlane")) {
            if (t >= 0) {
                optionalResearch_ = true;
                onlyColour_ = int(TechColor::Blue);
                researchStep(t, 0, "Hyperlane Development Program");
                onlyColour_ = -1;
                optionalResearch_ = false;
            }
        } else if (is("absol_nexus") || is("absol_travelban") || is("absol_recon")) {
            // (adjacency: addLaw)
        } else if (is("absol_pax")) {
            absolLockCard(5, "Pax Magnifica, Bellum Gloriosum");
            if (t >= 0) {
                const int k = std::min(2, reinforcementTokens(t));
                G.players[size_t(t)].strategy = int8_t(G.players[size_t(t)].strategy + k);
                if (k) say(t, pn(t) + " adds " + std::to_string(k) + " command tokens to their strategy pool.");
            }
            // "Players reveal all Ceasefire promissory notes in their hand, then in voting order, give each Ceasefire matching
            // their color to another player"
            std::string shown;
            for (int q = 0; q < n; ++q)
                for (int16_t note : G.players[size_t(q)].notes)
                    if (noteType(note) == kNoteCeasefire) shown += (shown.empty() ? "" : ", ") + pn(q) + " holds " + noteNameOf(G, note);
            if (!shown.empty()) say(-1, "Ceasefires revealed: " + shown + ".");
            std::vector<int> order = clockwiseFrom(G, (G.speaker + 1) % n);
            for (int q : order) {
                for (int16_t note : std::vector<int16_t>(G.players[size_t(q)].notes)) {
                    if (noteType(note) != kNoteCeasefire || noteOwner(note) != q) continue;
                    std::vector<Option> opts;
                    for (int r = 0; r < n; ++r)
                        if (r != q && !G.players[size_t(r)].eliminated) opts.push_back({r, pn(r), ""});
                    if (opts.empty()) continue;
                    curValue_ = kAbPaxCeasefire;
                    const int r = opts.size() == 1 ? opts[0].id
                                                   : pick(q, Tag::AbsolGiveTo, "Pax Magnifica: give your Ceasefire to which player?", opts);
                    curValue_ = 0;
                    giveNote(q, r, note);
                }
            }
        } else if (is("absol_revolution")) {
            // "Each player who voted For or abstained returns 1 technology they own with the most prerequisites"
            for (int q : forOrAbstained()) {
                Player& pl = G.players[size_t(q)];
                int most = -1;
                for (int16_t x : pl.techs)
                    most = std::max(most, sumPrereqs(x));
                if (most < 0) continue;
                std::vector<Option> opts;
                for (int16_t x : pl.techs)
                    if (sumPrereqs(x) == most) opts.push_back({x, kTechs[x].name, std::to_string(most) + " prerequisites"});
                curValue_ = kAbRevolutionTech;
                const int x = opts.size() == 1 ? opts[0].id
                                               : pick(q, Tag::AbsolChoice, "Anti-Intellectual Revolution: return which technology?", opts);
                curValue_ = 0;
                pl.techs.erase(std::find(pl.techs.begin(), pl.techs.end(), int16_t(x)));
                refreshUnits(q);
                say(q, pn(q) + " returns " + kTechs[x].name + " to the technology deck (Anti-Intellectual Revolution).");
            }
        } else if (is("absol_shared")) {
            // "may explore any frontier tokens in systems that contain anomalies and their ships"
            if (t >= 0) {
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (G.systems[s].frontier && anomaliesFor(G, t, int(s)) && hasShips(G, t, int(s))) opts.push_back({int(s), sysName(int(s)), ""});
                if (!opts.empty()) {
                    curValue_ = kAbSharedExplore;
                    const std::vector<int> picks =
                        pickMany(t, Tag::AbsolChoice, "Shared Research: explore which frontier tokens?", opts, 0, int(opts.size()));
                    curValue_ = 0;
                    for (int s : picks) exploreFrontier(t, s);
                }
            }
        } else if (is("absol_sites")) {
            if (t >= 0) {
                int k = 0;
                for (int x : controlledPlanets(G, t))
                    if (isLegendaryPlanet(G, x) || !G.planets[size_t(x)].attachments.empty()) ++k;
                gainTG(t, 2 * k);
            }
        } else if (is("absol_affirmation")) {
            // (law in play: Mecatol Rex is legendary; the notes come at the end of each strategy phase)
        }
        if (is("absol_travelban") || is("absol_recon") || is("absol_nexus")) computeAdjacency(G);
        return;
    }

    // Against outcomes of laws
    if (is("absol_affirmation")) {
        // "Exhaust the Imperial strategy card. Place this card with the current laws, and place the custodians token on it. The
        // player who had the custodians token loses 1 victory point."
        absolLockCard(7, "Affirmation of Divine Right");
        addLaw(a, -1, -1);
        if (Law* l = lawByKeyMut(G, "absol_affirmation")) l->against = true;
        if (G.custodiansRemoved && G.custodiansBy >= 0 && !G.players[size_t(G.custodiansBy)].eliminated) {
            loseVP(G.custodiansBy, 1, "Affirmation of Divine Right: the custodians token");
        } else if (!G.custodiansRemoved) {
            G.custodiansRemoved = true;  // (the token leaves Mecatol Rex for this card)
            say(-1, "The custodians token leaves Mecatol Rex for Affirmation of Divine Right.");
        }
        G.custodiansBy = -1;
        return;
    }
    G.agendaDiscard.push_back(int16_t(a));
    const int t = top(1);
    if (is("absol_abolishment")) {
        // "Reveal cards from the bottom of the agenda deck until a law is revealed. Put that law into play. The player that cast
        // the most votes Against is considered to have cast the most votes for that law and chooses what outcome is elected"
        const int law = absolRevealFromBottom();
        if (law < 0) return;
        std::vector<Option> outs = agendaOutcomes(law);
        if (outs.empty()) {
            G.agendaDiscard.push_back(int16_t(law));
            say(-1, std::string(kAgendas[law].name) + " can't be put into play - it is discarded.");
            return;
        }
        int out = 0;
        if (kAgendas[law].elect != Elect::ForAgainst) {
            const int chooser = t >= 0 ? t : G.speaker;
            const int prev = curAgenda_;
            curAgenda_ = law;
            curValue_ = kAbBottomOutcome;
            out = pick(chooser, Tag::AbsolChoice, std::string("Judicial Abolishment: choose the outcome of ") + kAgendas[law].name, outs,
                       agendaText(law, false));
            curValue_ = 0;
            curAgenda_ = prev;
        }
        std::vector<int> fv(size_t(n), kNoVote), fc(size_t(n), 0);
        if (t >= 0) {
            fv[size_t(t)] = out;
            fc[size_t(t)] = 1;
        }
        say(-1, std::string(kAgendas[law].name) + " is put into play.");
        resolveOutcome(law, out, fv, fc);
    } else if (is("absol_articleswar")) {
        for (int q : forOrAbstained()) {
            for (size_t x = 0; x < G.planets.size(); ++x) {
                const int k = G.planets[x].units[size_t(q)].count(UnitType::Mech);
                if (!k) continue;
                G.planets[x].units[size_t(q)].remove(UnitType::Mech, k, true);
                G.planets[x].units[size_t(q)].add(UnitType::Infantry, k);
            }
            for (size_t s = 0; s < G.systems.size(); ++s) {
                const int k = G.systems[s].space[size_t(q)].count(UnitType::Mech);
                if (!k) continue;
                G.systems[s].space[size_t(q)].remove(UnitType::Mech, k, true);
                G.systems[s].space[size_t(q)].add(UnitType::Infantry, k);
            }
            say(q, pn(q) + " replaces their mechs with infantry (Articles of War).");
        }
        // "may place any number of mechs in their reinforcements on any planet or planets they control"
        for (int guard = 0; t >= 0 && guard < 12 && unitsForPlacement(G, t, UnitType::Mech) > 0; ++guard) {
            std::vector<Option> opts;
            for (int x : controlledPlanets(G, t))
                if (!isStation(G, x)) opts.push_back({x, planetName(x), ""});
            if (opts.empty()) break;
            opts.push_back({-1, "Stop placing mechs", ""});
            curValue_ = kAbArticlesMechs;
            const int x = pick(t, Tag::AbsolChoice, "Articles of War: place a mech on which planet?", opts, "", false, true);
            curValue_ = 0;
            if (x < 0) break;
            addUnits(t, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
        }
    } else if (is("absol_checks")) {
        // "Remove all control tokens from public objectives and shuffle all scored secret objectives back into the deck. Players do
        // not gain or lose victory points as a result of this."
        for (int q = 0; q < n; ++q) {
            Player& pl = G.players[size_t(q)];
            std::vector<int16_t> keep;
            for (int16_t ob : pl.scored) {
                if (scoredAsSecret(G, q, ob)) G.secretDeck.push_back(ob);
                // (public: its control token comes off)
            }
            pl.scored = keep;
            pl.scoredAsPublic.clear();
        }
        E.rng().shuffle(G.secretDeck);
        say(-1, "Every control token comes off the public objectives and the scored secret objectives are shuffled back into the deck.");
    } else if (is("absol_conscription")) {
        if (t >= 0) {
            const std::vector<int> most = mostTrait(t, Trait::Industrial);
            absolGiveNoteToOneOf(t, most, "Regulated Conscription");
        }
        PendingEffect pe;
        pe.agenda = int16_t(a);
        pe.against = true;
        for (int q : againstOrAbstained()) pe.players = uint8_t(pe.players | (1u << q));
        G.pending.push_back(pe);
    } else if (is("absol_conventionswar")) {
        for (int q : againstOrAbstained()) discardAllActionCards(q, "Conventions of War");
        if (t >= 0) absolGiveNoteToOneOf(t, mostTrait(t, Trait::Cultural), "Conventions of War");
    } else if (is("absol_defenseact")) {
        for (int q = 0; q < n; ++q) {
            std::vector<Option> opts;
            for (size_t x = 0; x < G.planets.size(); ++x)
                if (G.planets[x].units[size_t(q)].count(UnitType::PDS)) opts.push_back({int(x), "PDS on " + planetName(int(x)), ""});
            if (opts.empty()) continue;
            curValue_ = kAbDefenseDestroy;
            const int x = opts.size() == 1 ? opts[0].id : pick(q, Tag::AbsolChoice, "Homeland Defense Act: destroy which PDS?", opts);
            curValue_ = 0;
            destroyUnits(q, UnitType::PDS, 1, G.planets[size_t(x)].system, x, -1, "Homeland Defense Act");
        }
        std::vector<int> forVoters = voters(0);
        for (int q : againstOrAbstained()) absolGiveNoteToOneOf(q, forVoters, "Homeland Defense Act");
    } else if (is("absol_equality")) {
        if (t >= 0) {
            const int gain = G.players[size_t(t)].tradeGoods;
            gainTG(t, gain);
            const int loss = gain / 3;
            for (int q = 0; q < n && loss > 0; ++q) {
                if (q == t || G.players[size_t(q)].eliminated) continue;
                const int k = std::min<int>(loss, G.players[size_t(q)].tradeGoods);
                G.players[size_t(q)].tradeGoods = int8_t(G.players[size_t(q)].tradeGoods - k);
                if (k) say(q, pn(q) + " loses " + std::to_string(k) + " trade good" + (k > 1 ? "s" : "") + " (Economic Equality).");
            }
        }
    } else if (is("absol_government")) {
        PendingEffect pe;
        pe.agenda = int16_t(a);
        pe.against = true;
        for (int q : againstOrAbstained()) pe.players = uint8_t(pe.players | (1u << q));
        G.pending.push_back(pe);
    } else if (is("absol_hyperlane")) {
        absolLockCard(4, "Hyperlane Development Program");
        for (int q = 0; q < n; ++q) {
            Player& pl = G.players[size_t(q)];
            const int k = std::min<int>(3, pl.tradeGoods);
            pl.tradeGoods = int8_t(pl.tradeGoods - k);
            if (k) say(q, pn(q) + " returns " + std::to_string(k) + " trade goods (Hyperlane Development Program).");
        }
        if (t >= 0)
            for (size_t s = 0; s < G.systems.size(); ++s) {
                if (!hasShips(G, t, int(s)) || !G.systems[s].planets.empty() || anomaliesFor(G, t, int(s))) continue;
                std::vector<Option> opts;
                if (unitsForPlacement(G, t, UnitType::Cruiser) > 0) opts.push_back({int(UnitType::Cruiser), "A cruiser", ""});
                if (unitsForPlacement(G, t, UnitType::Destroyer) > 0) opts.push_back({int(UnitType::Destroyer), "A destroyer", ""});
                if (opts.empty()) break;
                opts.push_back({-1, "Nothing here", ""});
                curValue_ = kAbHyperShips;
                const int u = pick(t, Tag::AbsolChoice, "Hyperlane Development Program: place a ship in " + sysName(int(s)) + "?", opts);
                curValue_ = 0;
                if (u >= 0) {
                    addUnits(t, UnitType(u), 1, int(s), -1);
                    say(t, pn(t) + " places a " + unitTypeName(UnitType(u)) + " in " + sysName(int(s)) + ".", int(s));
                    enforceFleetLimit(t);
                }
            }
    } else if (is("absol_limits")) {
        if (t >= 0) drawActionCards(t, 2, "Term Limits");
        absolLockCard(2, "Term Limits");
        std::vector<int> forVoters = voters(0);
        for (int q : againstOrAbstained()) absolGiveNoteToOneOf(q, forVoters, "Term Limits");
    } else if (is("absol_nexus")) {
        const int mec = G.mecatol();
        if (mec >= 0) {
            G.systems[size_t(mec)].tokenWormholes |= kGamma;
            computeAdjacency(G);
            say(-1, "A gamma wormhole token is placed in the Mecatol Rex system.", mec);
        }
        if (t >= 0 && G.nexus >= 0) {
            std::vector<int> sys{G.nexus};
            for (int16_t x : G.systems[size_t(G.nexus)].adj) sys.push_back(x);
            for (int s : sys) {
                std::vector<Option> opts;
                for (int u : kShipIdx)
                    if (u != int(UnitType::Fighter) && G.systems[size_t(s)].space[size_t(t)].n[u])
                        opts.push_back({u, std::string(unitTypeName(UnitType(u))) + " in " + sysName(s), ""});
                if (opts.empty()) continue;
                curValue_ = kAbNexusDestroy;
                const int u = opts.size() == 1 ? opts[0].id : pick(t, Tag::AbsolChoice, "Nexus Sovereignty: destroy which ship in " + sysName(s) + "?", opts);
                curValue_ = 0;
                destroyUnits(t, UnitType(u), 1, s, -1, -1, "Nexus Sovereignty");
                removeExcessCapacity(t, s);
            }
            checkEliminations();
        }
    } else if (is("absol_pax")) {
        absolLockCard(1, "Pax Magnifica, Bellum Gloriosum");
        if (t >= 0) {
            const int k = std::min(2, std::min(reinforcementTokens(t), std::max(0, fleetPoolCap(G, t) - G.players[size_t(t)].fleet)));
            G.players[size_t(t)].fleet = int8_t(G.players[size_t(t)].fleet + k);
            if (k) say(t, pn(t) + " adds " + std::to_string(k) + " command tokens to their fleet pool.");
        }
        // "Players may use the PRODUCTION ability of any number of their units. After resolving this outcome, skip to the Ready
        // Planets step of the agenda phase."
        for (int q : clockwiseFrom(G, (G.speaker + 1) % n)) {
            if (G.players[size_t(q)].eliminated) continue;
            std::vector<int> done;
            for (int guard = 0; guard < 12; ++guard) {
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (std::find(done.begin(), done.end(), int(s)) == done.end() && productionCapacity(G, q, int(s)) > 0 &&
                        !blockaded(G, q, int(s)))
                        opts.push_back({int(s), sysName(int(s)), "PRODUCTION " + std::to_string(productionCapacity(G, q, int(s)))});
                if (opts.empty()) break;
                opts.push_back({-1, "Done producing", ""});
                curValue_ = kAbPaxProduce;
                const int s = pick(q, Tag::AbsolChoice, "Pax Magnifica, Bellum Gloriosum: use PRODUCTION in which system?", opts, "", true);
                curValue_ = 0;
                if (s < 0) break;
                done.push_back(s);
                production(q, s, productionCapacity(G, q, s), false);
            }
        }
        absolSkip_ = true;
        say(-1, "Pax Magnifica, Bellum Gloriosum: the agenda phase skips to readying planets.");
    } else if (is("absol_recon") || is("absol_shared")) {
        // "The player who cast the most votes For takes 1 command token from the reinforcements of each player who voted Against or
        // abstained and places them ..." (Wormhole Reconstruction: in systems with alpha or beta wormholes; Shared Research: anomalies)
        const int f = top(0);
        if (f >= 0)
            for (int q : againstOrAbstained()) {
                if (reinforcementTokens(q) <= 0) continue;
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    if (G.systems[s].hasToken(q)) continue;
                    const bool fits = is("absol_recon") ? (wormholesOf(G, int(s)) & (kAlpha | kBeta)) != 0 : G.systems[s].d().anomalies != 0;
                    if (fits) opts.push_back({int(s), sysName(int(s)), ""});
                }
                if (opts.empty()) continue;
                const int prevF = curFor_;
                curFor_ = q;
                curValue_ = kAbPlaceTheirToken;
                const int s = pick(f, Tag::AbsolChoice, std::string(kAgendas[a].name) + ": place " + pn(q) + "'s command token in which system?",
                                   opts, "", true);
                curValue_ = 0;
                curFor_ = prevF;
                G.systems[size_t(s)].tokens |= uint16_t(1u << q);
                say(f, pn(f) + " places " + pn(q) + "'s command token in " + sysName(s) + ".", s);
            }
    } else if (is("absol_regulations")) {
        if (t >= 0) {
            const int k = std::min(2, std::min(reinforcementTokens(t), std::max(0, fleetPoolCap(G, t) - G.players[size_t(t)].fleet)));
            G.players[size_t(t)].fleet = int8_t(G.players[size_t(t)].fleet + k);
            if (k) say(t, pn(t) + " places " + std::to_string(k) + " command tokens in their fleet pool.");
        }
    } else if (is("absol_revolution")) {
        absolLockCard(6, "Anti-Intellectual Revolution");
        // "destroys a number of their units on the game board with a combined cost of at least 6 resources"
        for (int q : againstOrAbstained()) {
            std::vector<Option> opts;
            int total2 = 0;  // costs x2
            for (size_t s = 0; s < G.systems.size(); ++s) {
                for (int u = 0; u < kUnitTypes; ++u)
                    for (int i = 0; i < G.systems[s].space[size_t(q)].n[u] && i < 64; ++i) {  // id: s*65536 + loc*1024 + type*64 + i
                        const int c2 = unitDef(G, q, UnitType(u)).cost2;
                        if (c2 <= 0) continue;
                        opts.push_back({int(s) * 65536 + u * 64 + i, std::string(unitTypeName(UnitType(u))) + " in " + sysName(int(s)),
                                        "Cost " + std::to_string(c2 / 2) + (c2 % 2 ? ".5" : "")});
                        total2 += c2;
                    }
                for (size_t k = 0; k < G.systems[s].planets.size() && k < 15; ++k) {
                    const int x = G.systems[s].planets[k];
                    for (int u = 0; u < kUnitTypes; ++u)
                        for (int i = 0; i < G.planets[size_t(x)].units[size_t(q)].n[u] && i < 64; ++i) {
                            const int c2 = unitDef(G, q, UnitType(u)).cost2;
                            if (c2 <= 0) continue;
                            opts.push_back({int(s) * 65536 + 1024 * int(k + 1) + u * 64 + i, std::string(unitTypeName(UnitType(u))) + " on " + planetName(x),
                                            "Cost " + std::to_string(c2 / 2) + (c2 % 2 ? ".5" : "")});
                            total2 += c2;
                        }
                }
            }
            if (opts.empty()) continue;
            std::vector<int> picks;
            if (total2 <= 12) {
                for (const Option& op : opts) picks.push_back(op.id);
            } else {
                curValue_ = kAbRevolutionDestroy;
                const std::vector<Option> copy = opts;
                for (int guard = 0; guard < 40; ++guard) {
                    picks = pickMany(q, Tag::AbsolChoice, "Anti-Intellectual Revolution: destroy units with a combined cost of at least 6", copy, 1,
                                     int(copy.size()), "Choose the units; their costs must add up to 6 or more.");
                    int sum = 0;
                    for (int id : picks) sum += unitDef(G, q, UnitType((id % 1024) / 64)).cost2;
                    if (sum >= 12) break;
                    say(q, "Those units cost less than 6 - choose again.");
                }
                curValue_ = 0;
            }
            // destroy: group by system / location / type
            for (int id : picks) {
                const int s = id / 65536, loc = (id % 65536) / 1024, u = (id % 1024) / 64;
                const int planet = loc == 0 ? -1 : G.systems[size_t(s)].planets[size_t(loc - 1)];
                const Force& f = planet < 0 ? G.systems[size_t(s)].space[size_t(q)] : G.planets[size_t(planet)].units[size_t(q)];
                if (f.count(UnitType(u)) > 0) destroyUnits(q, UnitType(u), 1, s, planet, -1, "Anti-Intellectual Revolution");
            }
            for (size_t s = 0; s < G.systems.size(); ++s) removeExcessCapacity(q, int(s));
        }
        checkEliminations();
    } else if (is("absol_sanctions")) {
        if (t >= 0) drawActionCards(t, 5, "Executive Sanctions");
    } else if (is("absol_sites")) {
        if (t >= 0) {
            std::vector<Option> opts;
            for (size_t s = 0; s < G.systems.size(); ++s) {
                int k = 0;
                for (int16_t x : G.systems[s].planets)
                    if (G.planets[size_t(x)].owner == t && !isStation(G, x)) ++k;
                if (k) opts.push_back({int(s), sysName(int(s)), std::to_string(k) + " of your planets"});
            }
            if (!opts.empty()) {
                opts.push_back({-1, "Don't explore", ""});
                curValue_ = kAbSitesAgainst;
                const int s = pick(t, Tag::AbsolChoice, "Preservation of Historical Sites: explore each planet you control in which system?", opts,
                                   "", true);
                curValue_ = 0;
                if (s >= 0)
                    for (int16_t x : std::vector<int16_t>(G.systems[size_t(s)].planets))
                        if (G.planets[size_t(x)].owner == t && !isStation(G, x)) explorePlanet(t, x);
            }
        }
    } else if (is("absol_travelban")) {
        // "Players who voted Against gain 1 trade good for each wormhole in systems that contain 1 or more of their units. Players that
        // gained trade goods also gain 1 commodity for each of their neighbors."
        for (int q : voters(1)) {
            int k = 0;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (hasUnits(G, q, int(s))) {
                    const uint8_t w = wormholesOf(G, int(s));
                    for (int b = 0; b < 8; ++b)
                        if ((w >> b) & 1) ++k;
                }
            if (k <= 0) continue;
            gainTG(q, k);
            int nb = 0;
            for (int r = 0; r < n; ++r)
                if (r != q && !G.players[size_t(r)].eliminated && areNeighbours(G, q, r)) ++nb;
            Player& pl = G.players[size_t(q)];
            const int before = pl.commodities;
            pl.commodities = int8_t(std::min<int>(commodityCap(G, pl), pl.commodities + nb));
            if (pl.commodities > before) {
                say(q, pn(q) + " gains " + std::to_string(pl.commodities - before) + " commodities (Enforced Travel Ban).");
                commodityGained(q, before);
            }
        }
    }
}

void Rules::absolDirectiveOutcome(int a, int o, const std::vector<int>& votedFor, const std::vector<int>& votes) {
    const int n = G.numPlayers;
    auto is = [&](const char* k) { return abKey(a, k); };
    const Elect el = kAgendas[a].elect;
    const bool against = el == Elect::ForAgainst && o == 1;
    auto top = [&](int out) { return absolTop(a, out, votedFor, votes); };
    auto voters = [&](int out) { return absolVotersByMost(out, votedFor, votes); };
    auto againstOrAbstained = [&]() {
        std::vector<int> v;
        for (int q = 0; q < n; ++q)
            if ((q < int(votedFor.size()) && votedFor[size_t(q)] == 1 && votes[size_t(q)] > 0) || absolAbstained(q, votedFor, votes))
                v.push_back(q);
        return v;
    };
    auto addToPool = [&](int p, int pool, int k, const char* why) {  // 0 tactic, 1 fleet, 2 strategy - from the reinforcements
        if (p < 0) return;
        Player& pl = G.players[size_t(p)];
        if (pool == 1) k = std::min(k, std::max(0, fleetPoolCap(G, p) - pl.fleet));
        k = std::min(k, reinforcementTokens(p));
        if (k <= 0) return;
        (pool == 0 ? pl.tactic : pool == 1 ? pl.fleet : pl.strategy) += int8_t(k);
        say(p, pn(p) + " adds " + std::to_string(k) + " command token" + (k > 1 ? "s" : "") + " to their " +
                   (pool == 0 ? "tactic" : pool == 1 ? "fleet" : "strategy") + " pool (" + why + ").");
    };
    auto leastOrMost = [&](bool most, const char* why) {  // the player with the most / least points (the speaker breaks ties)
        int v = most ? -1 : 999;
        for (int q = 0; q < n; ++q)
            if (!G.players[size_t(q)].eliminated) v = most ? std::max<int>(v, G.players[size_t(q)].vp) : std::min<int>(v, G.players[size_t(q)].vp);
        std::vector<Option> opts;
        for (int q = 0; q < n; ++q)
            if (!G.players[size_t(q)].eliminated && G.players[size_t(q)].vp == v) opts.push_back({q, pn(q), std::to_string(v) + " victory points"});
        if (opts.size() == 1) return opts[0].id;
        curValue_ = most ? kAbTieMostVP : kAbTieLeastVP;
        const int w = pick(G.speaker, Tag::AbsolTie, std::string(why) + ": tied - you are the speaker, choose the player", opts);
        curValue_ = 0;
        return w;
    };
    G.agendaDiscard.push_back(int16_t(a));

    if (is("absol_agreement")) {  // Rearmament Agreement
        const int t = top(against ? 1 : 0);
        if (!against) {
            for (int q : clockwiseFrom(G, G.speaker)) {
                if (G.players[size_t(q)].eliminated || unitsForPlacement(G, q, UnitType::Mech) <= 0) continue;
                std::vector<Option> opts;
                for (int16_t x : G.systems[size_t(G.players[size_t(q)].homeSystem)].planets)
                    if (G.planets[size_t(x)].owner == q) opts.push_back({x, planetName(x), ""});
                if (opts.empty()) continue;
                curValue_ = kAbAgreementMech;
                const int x = opts.size() == 1 ? opts[0].id : pick(q, Tag::AbsolChoice, "Rearmament Agreement: place a mech on which home planet?", opts, "", false, true);
                curValue_ = 0;
                addUnits(q, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
            }
            if (t >= 0 && unitsForPlacement(G, t, UnitType::Mech) > 0) {
                std::vector<Option> opts;
                for (int x : controlledPlanets(G, t))
                    if (!isStation(G, x)) opts.push_back({x, planetName(x), ""});
                if (!opts.empty()) {
                    curValue_ = kAbAgreementMech;
                    const int x = pick(t, Tag::AbsolChoice, "Rearmament Agreement: place another mech on which planet?", opts, "", false, true);
                    curValue_ = 0;
                    addUnits(t, UnitType::Mech, 1, G.planets[size_t(x)].system, x);
                }
            }
            addToPool(t, 1, 1, "Rearmament Agreement");
        } else {
            for (int q = 0; q < n; ++q) {
                bool any = false;
                for (size_t x = 0; x < G.planets.size(); ++x) {
                    const int k = G.planets[x].units[size_t(q)].count(UnitType::Mech);
                    if (!k) continue;
                    G.planets[x].units[size_t(q)].remove(UnitType::Mech, k, true);
                    G.planets[x].units[size_t(q)].add(UnitType::Infantry, k);
                    any = true;
                }
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    const int k = G.systems[s].space[size_t(q)].count(UnitType::Mech);
                    if (!k) continue;
                    G.systems[s].space[size_t(q)].remove(UnitType::Mech, k, true);
                    G.systems[s].space[size_t(q)].add(UnitType::Infantry, k);
                    any = true;
                }
                if (any) say(q, pn(q) + " replaces their mechs with infantry (Rearmament Agreement).");
            }
            if (t >= 0) {
                int k = 0;
                for (int x : controlledPlanets(G, t))
                    if (!isStation(G, x) && G.planets[size_t(x)].units[size_t(t)].groundForces() == 0) ++k;
                addToPool(t, 2, k / 2, "Rearmament Agreement");
            }
        }
    } else if (is("absol_artifact")) {  // Ixthian Artifact
        if (!against) {
            const int v = E.rng().d10();
            say(G.speaker, pn(G.speaker) + " rolls " + std::to_string(v) + " for the Ixthian Artifact.");
            if (v >= 7) {
                optionalResearch_ = true;  // "each player may research 2 technologies"
                for (int q : clockwiseFrom(G, G.speaker)) {
                    researchStep(q, 0, "Ixthian Artifact");
                    researchStep(q, 0, "Ixthian Artifact");
                }
                optionalResearch_ = false;
            } else {
                ixthianFails();
            }
        } else {
            // "Attach this card to the Mecatol Rex planet card. The player who controls Mecatol Rex gains 1 victory point, and loses 1
            // victory point if they lose control of Mecatol Rex." (RULES-CHECK AB2: whoever controls it after them holds the point)
            G.mecArtifact = true;
            const int mec = G.mecatol();
            int owner = -1;
            if (mec >= 0)
                for (int16_t x : G.systems[size_t(mec)].planets)
                    if (G.planets[size_t(x)].owner >= 0) owner = G.planets[size_t(x)].owner;
            say(-1, "The Ixthian Artifact is attached to Mecatol Rex.");
            if (owner >= 0) gainVP(owner, 1, "Ixthian Artifact");
        }
    } else if (is("absol_constitution")) {  // New Constitution
        if (!against) {
            while (!G.laws.empty()) discardLaw(int(G.laws.size()) - 1, "New Constitution");
            const int t = top(0);
            if (t >= 0) absolSetSpeaker(t, "New Constitution");
            const int law = absolRevealFromBottom();
            if (law >= 0) {
                say(-1, std::string("Players vote on ") + kAgendas[law].name + " as if it had just been revealed.");
                runAgenda(law, false);
            }
        } else {
            std::vector<int> forVoters = voters(0);
            for (int q : againstOrAbstained()) absolGiveNoteToOneOf(q, forVoters, "New Constitution");
        }
    } else if (is("absol_crisis")) {  // Galactic Crisis Pact (elect a strategy card)
        const int t = top(o);
        for (int q : clockwiseFrom(G, G.speaker)) {
            if (G.players[size_t(q)].eliminated) continue;
            if (q == t && o != 0 && G.players[size_t(q)].strategy > 0) {
                curValue_ = kAbCrisisPrimary;
                const bool prim = yesNo(q, Tag::AbsolChoice,
                                        std::string("Galactic Crisis Pact: spend a strategy token to resolve the primary ability of ") + strategyName(o) + " instead?",
                                        strategyPrimary(o));
                curValue_ = 0;
                if (prim) {
                    spendStrategy(q, 1);
                    say(q, pn(q) + " resolves the primary ability of " + strategyName(o) + " (Galactic Crisis Pact).");
                    primaryAbility(q, o);
                    continue;
                }
            }
            absolCrisisReinf_ = true;
            secondaryForFree(q, o);
            absolCrisisReinf_ = false;
        }
    } else if (is("absol_disarmament")) {  // Compensated Disarmament (elect a non-home planet)
        const int owner = G.planets[size_t(o)].owner;
        int gained = 0;
        removing_ = true;
        for (int q = 0; q < n; ++q)
            for (UnitType u : {UnitType::Infantry, UnitType::Mech}) {
                const int k = G.planets[size_t(o)].units[size_t(q)].count(u);
                if (!k) continue;
                destroyUnits(q, u, k, G.planets[size_t(o)].system, o, -1, "Compensated Disarmament");
                gained += k * (u == UnitType::Mech ? 2 : 1);
            }
        removing_ = false;
        if (owner >= 0) {
            gainTG(owner, gained);
            const int t = absolTop(a, o, votedFor, votes, owner);
            if (t >= 0) absolGiveNote(t, owner, "Compensated Disarmament");
        }
        checkEliminations();
    } else if (is("absol_execution")) {  // Public Execution
        for (int x : controlledPlanets(G, o)) G.planets[size_t(x)].exhausted = true;
        Player& el2 = G.players[size_t(o)];
        std::string hand;
        for (int16_t c : el2.actionCards) hand += (hand.empty() ? "" : ", ") + std::string(kActionCards[c].name);
        say(o, pn(o) + " reveals their action cards: " + (hand.empty() ? std::string("none") : hand) + ".");
        E.revealed();
        for (int q : voters(o)) {
            if (q == o || el2.actionCards.empty()) continue;
            std::vector<Option> opts;
            for (int16_t c : el2.actionCards) {
                bool dup = false;
                for (const Option& op : opts) dup = dup || op.id == c;
                if (!dup) opts.push_back({c, kActionCards[c].name, actionCardText(c)});
            }
            opts.push_back({-1, "Take none", ""});
            curValue_ = kAbExecutionTake;
            const int c = pick(q, Tag::AbsolChoice, "Public Execution: take which action card from " + pn(o) + "'s hand?", opts);
            curValue_ = 0;
            if (c < 0) continue;
            el2.actionCards.erase(std::find(el2.actionCards.begin(), el2.actionCards.end(), int16_t(c)));
            G.players[size_t(q)].actionCards.push_back(int16_t(c));
            say(q, pn(q) + " takes " + kActionCards[c].name + " from " + pn(o) + ".");
        }
        discardAllActionCards(o, "Public Execution");
        if (G.speaker == o) {
            const int t = absolTop(a, o, votedFor, votes, o);
            if (t >= 0) absolSetSpeaker(t, "Public Execution");  // (Political Censure / Term Limits still apply)
        }
    } else if (is("absol_grant")) {  // Research Grant Reallocation
        const std::vector<int16_t> before = G.players[size_t(o)].techs;
        absolNoFaction_ = true;
        optionalResearch_ = true;
        researchStep(o, 0, "Research Grant Reallocation");
        optionalResearch_ = false;
        absolNoFaction_ = false;
        int got = -1;
        for (int16_t x : G.players[size_t(o)].techs)
            if (std::find(before.begin(), before.end(), x) == before.end()) got = x;
        const int t = absolTop(a, o, votedFor, votes, o);
        if (got >= 0 && t >= 0 && !G.players[size_t(t)].hasTech(got) && techAvailableToResearch(G, t, got)) {
            const int cost = 2 * sumPrereqs(got);
            if (G.players[size_t(t)].tradeGoods >= cost) {
                curValue_ = kAbGrantGain;
                const bool yes = yesNo(t, Tag::AbsolChoice,
                                       "Research Grant Reallocation: spend " + std::to_string(cost) + " trade goods to gain " + kTechs[got].name + "?",
                                       techSummary(got));
                curValue_ = 0;
                if (yes) {
                    G.players[size_t(t)].tradeGoods = int8_t(G.players[size_t(t)].tradeGoods - cost);
                    researchTech(t, got, false);
                    E.log(t, pn(t) + " gains " + kTechs[got].name + ".", EventKind::Research);
                }
            }
        }
    } else if (is("absol_incentive")) {  // Incentive Program
        std::vector<int16_t>& rest = against ? G.stage2Rest : G.stage1Rest;
        if (!rest.empty()) {
            const int ob = rest.back();
            rest.pop_back();
            G.publicObjectives.push_back(int16_t(ob));
            E.log(-1, std::string("New public objective: ") + kObjectives[ob].name + " (" + std::to_string(kObjectives[ob].points) + " VP) - " +
                          objectiveSummary(ob),
                  EventKind::Phase);
            const int t = top(against ? 1 : 0);
            if (t >= 0) {
                // (Absol's FAQ: the Minister of Antiquities may draw a relic instead - and still draws the action cards if the
                // objective's requirements aren't met)
                const bool meets = meetsObjective(G, t, ob);
                if (absolScoreInstead(t, meets ? ob : -1)) {
                    if (!meets) drawActionCards(t, against ? 3 : 2, "Incentive Program");
                } else if (!(meets && scoreObjective(t, ob))) {
                    drawActionCards(t, against ? 3 : 2, "Incentive Program");
                }
            }
        }
    } else if (is("absol_measures")) {  // Unconventional Measures
        if (!against) {
            for (int q : voters(0)) drawActionCards(q, 2, "Unconventional Measures");
            for (int q : voters(1)) gainTokens(q, 1, false, "Unconventional Measures: gain 1 command token.");
        } else {
            for (int q : againstOrAbstained()) discardAllActionCards(q, "Unconventional Measures");
            const int f = top(0);
            if (f >= 0) {
                std::vector<Option> opts;
                for (int c = 0; c < kStrategyCards; ++c)
                    if (!((G.scLocked >> c) & 1)) opts.push_back({c, std::to_string(c + 1) + " " + strategyName(c), ""});
                if (!opts.empty()) {
                    curValue_ = kAbMeasuresExhaust;
                    const int c = pick(f, Tag::AbsolChoice, "Unconventional Measures: which strategy card becomes exhausted?", opts,
                                       "Nobody can choose it in the next strategy phase.");
                    curValue_ = 0;
                    absolLockCard(c, "Unconventional Measures");
                }
            }
        }
    } else if (is("absol_miscount")) {  // Miscount Disclosed (elect a law)
        for (size_t i = 0; i < G.laws.size(); ++i)
            if (G.laws[i].agenda == o) {
                if (!agendaRevealable(o) || agendaOutcomes(o).empty()) {
                    discardLaw(int(i), "Miscount Disclosed: nothing to vote for");
                    break;
                }
                const int prevLaw = miscountLaw_, prevM = absolMiscount_;
                miscountLaw_ = o;
                absolMiscount_ = top(o);  // "casts 1 additional vote per player when voting on the elected law"
                runAgenda(o, true);
                if (miscountLaw_ == o) liftLaw(o);
                miscountLaw_ = prevLaw;
                absolMiscount_ = prevM;
                break;
            }
    } else if (is("absol_mutiny")) {
        bool allFor = true, noneFor = true;
        for (int q = 0; q < n; ++q) {
            if (!absolVoting(q, votedFor)) continue;  // (FAQ: "Only the players who voted or abstained are counted")
            const bool f = q < int(votedFor.size()) && votedFor[size_t(q)] == 0 && votes[size_t(q)] > 0;
            if (f) noneFor = false;
            else allFor = false;
        }
        if (!against) {
            absolLockCard(2, "Mutiny");
            const int t = top(0);
            if (t >= 0) absolSetSpeaker(t, "Mutiny");
            for (int q : voters(0)) gainVP(q, 1, "Mutiny");
            if (allFor) gainVP(G.speaker, 1, "Mutiny: every player voted For");
        } else {
            for (int q : voters(0)) {
                loseVP(q, 1, "Mutiny");
                absolGiveNote(q, G.speaker, "Mutiny");
            }
            if (noneFor) {
                gainVP(G.speaker, 1, "Mutiny");
                const int t = absolTop(a, 1, votedFor, votes, G.speaker);
                if (t >= 0) gainVP(t, 1, "Mutiny");
            }
        }
    } else if (is("absol_operations")) {  // Clandestine Operations
        if (!against) {
            const int t = top(0);
            if (t >= 0) {
                gainTokens(t, 2, false, "Clandestine Operations: gain 2 command tokens.");
                absolSetSpeaker(t, "Clandestine Operations");
            }
            // "each player removes 1 command token from the command sheet of the player to their left"
            for (int q : clockwiseFrom(G, G.speaker)) {
                if (G.players[size_t(q)].eliminated) continue;
                std::vector<int> left = clockwiseFrom(G, (q + 1) % n);
                int l = -1;
                for (int r : left)
                    if (r != q && !G.players[size_t(r)].eliminated) {
                        l = r;
                        break;
                    }
                if (l < 0) continue;
                Player& pl = G.players[size_t(l)];
                std::vector<Option> opts;
                if (pl.tactic) opts.push_back({0, "Tactic pool", std::to_string(pl.tactic) + " tokens"});
                if (pl.fleet) opts.push_back({1, "Fleet pool", std::to_string(pl.fleet) + " tokens"});
                if (pl.strategy) opts.push_back({2, "Strategy pool", std::to_string(pl.strategy) + " tokens"});
                if (opts.empty()) continue;
                const int prevF = curFor_;
                curFor_ = l;
                curValue_ = kAbOperationsRemove;
                const int c = opts.size() == 1 ? opts[0].id
                                               : pick(q, Tag::AbsolChoice, "Clandestine Operations: remove a token from which of " + pn(l) + "'s pools?", opts);
                curValue_ = 0;
                curFor_ = prevF;
                (c == 0 ? pl.tactic : c == 1 ? pl.fleet : pl.strategy) -= 1;
                say(q, pn(q) + " removes one of " + pn(l) + "'s command tokens (" + (c == 0 ? "tactic" : c == 1 ? "fleet" : "strategy") + " pool).");
                enforceFleetLimit(l);
            }
        } else {
            for (int q = 0; q < n; ++q)
                if (!G.players[size_t(q)].eliminated && G.players[size_t(q)].fleet > 0) {
                    --G.players[size_t(q)].fleet;
                    enforceFleetLimit(q);
                }
            say(-1, "Each player removes 1 command token from their fleet pool.");
            absolLockCard(2, "Clandestine Operations");
        }
    } else if (is("absol_redistribution")) {  // Colonial Redistribution
        const int owner = G.planets[size_t(o)].owner;
        const int sys = G.planets[size_t(o)].system;
        int removed[kUnitTypes] = {};
        removing_ = true;
        for (int q = 0; q < n; ++q)
            for (int u = 0; u < kUnitTypes; ++u) {
                const int k = G.planets[size_t(o)].units[size_t(q)].n[u];
                if (!k) continue;
                if (q == owner) removed[u] += k;
                destroyUnits(q, UnitType(u), k, sys, o, -1, "Colonial Redistribution");
            }
        removing_ = false;
        if (owner >= 0) {
            // "places the removed units on 1 or more planets they control in their home system, if able"
            std::vector<int> home;
            for (int16_t x : G.systems[size_t(G.players[size_t(owner)].homeSystem)].planets)
                if (G.planets[size_t(x)].owner == owner) home.push_back(x);
            for (int u = 0; u < kUnitTypes && !home.empty(); ++u)
                for (int k = 0; k < removed[u]; ++k) {
                    if (unitsForPlacement(G, owner, UnitType(u)) <= 0) break;
                    int x = home[0];
                    if (home.size() > 1) {
                        std::vector<Option> opts;
                        for (int h : home) opts.push_back({h, planetName(h), ""});
                        curValue_ = kAbRedistPlace;
                        x = pick(owner, Tag::AbsolChoice, std::string("Colonial Redistribution: place your ") + unitTypeName(UnitType(u)) + " on which home planet?",
                                 opts, "", false, true);
                        curValue_ = 0;
                    }
                    addUnits(owner, UnitType(u), 1, G.planets[size_t(x)].system, x);
                }
            // "then chooses 1 other player with the fewest victory points; that player places 3 infantry" (Version 4: not optional)
            int low = 999;
            for (int q = 0; q < n; ++q)
                if (q != owner && !G.players[size_t(q)].eliminated) low = std::min<int>(low, G.players[size_t(q)].vp);
            std::vector<Option> opts;
            for (int q = 0; q < n; ++q)
                if (q != owner && !G.players[size_t(q)].eliminated && G.players[size_t(q)].vp == low) opts.push_back({q, pn(q), std::to_string(low) + " victory points"});
            if (!opts.empty()) {
                curValue_ = kAbRedistWho;
                const int who = opts.size() == 1 ? opts[0].id : pick(owner, Tag::AbsolChoice, "Colonial Redistribution: which player places 3 infantry?", opts);
                const int k = std::min(3, unitsForPlacement(G, who, UnitType::Infantry));
                if (k > 0) {
                    addUnits(who, UnitType::Infantry, k, sys, o);
                    say(who, pn(who) + " places " + std::to_string(k) + " infantry on " + planetName(o) + " (Colonial Redistribution).");
                    if (G.planets[size_t(o)].owner != who) setControl(o, who);
                }
                curValue_ = 0;
            }
        }
        checkEliminations();
    } else if (is("absol_reduction")) {  // Arms Reduction
        if (!against) {
            for (int q = 0; q < n; ++q) {
                if (G.players[size_t(q)].eliminated) continue;
                int tg = 0;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    const int d = G.systems[s].space[size_t(q)].count(UnitType::Dreadnought);
                    if (d) {
                        destroyUnits(q, UnitType::Dreadnought, d, int(s), -1, -1, "Arms Reduction");
                        tg += 2 * d;
                    }
                }
                const int cr = unitsOnBoard(G, q, UnitType::Cruiser), half = (cr + 1) / 2;
                if (half > 0) {
                    std::vector<Option> opts;
                    for (size_t s = 0; s < G.systems.size(); ++s)
                        for (int i = 0; i < G.systems[s].space[size_t(q)].count(UnitType::Cruiser); ++i)
                            opts.push_back({int(s) * 16 + std::min(i, 15), "Cruiser in " + sysName(int(s)), ""});
                    std::vector<int> picks;
                    if (half >= int(opts.size())) {
                        for (const Option& op : opts) picks.push_back(op.id);
                    } else {
                        curValue_ = kAbReductionCruisers;
                        picks = pickMany(q, Tag::AbsolChoice, "Arms Reduction: destroy " + std::to_string(half) + " cruiser" + (half > 1 ? "s" : ""), opts, half, half);
                        curValue_ = 0;
                    }
                    for (int id : picks)
                        if (G.systems[size_t(id / 16)].space[size_t(q)].count(UnitType::Cruiser)) {
                            destroyUnits(q, UnitType::Cruiser, 1, id / 16, -1, -1, "Arms Reduction");
                            tg += 1;
                        }
                }
                for (size_t s = 0; s < G.systems.size(); ++s) removeExcessCapacity(q, int(s));
                if (tg) gainTG(q, tg);
            }
            addToPool(top(0), 2, 1, "Arms Reduction");
            checkEliminations();
        } else {
            const int t = top(1);
            if (t >= 0) {
                optionalResearch_ = true;
                onlyUpgradeTypes_ = 0xFFFF;
                researchStep(t, 0, "Arms Reduction");
                onlyUpgradeTypes_ = 0;
                optionalResearch_ = false;
                // "then place 1 dreadnought or 2 cruisers in a system that contains only their units"
                std::vector<Option> opts;
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    if (!hasUnits(G, t, int(s)) || G.systems[s].isHyperlane()) continue;
                    bool only = !neutralUnitsIn(G, int(s));
                    for (int q = 0; q < n; ++q)
                        if (q != t && hasUnits(G, q, int(s))) only = false;
                    if (!only) continue;
                    if (unitsForPlacement(G, t, UnitType::Dreadnought) > 0) opts.push_back({int(s) * 2, "A dreadnought in " + sysName(int(s)), ""});
                    if (unitsForPlacement(G, t, UnitType::Cruiser) > 0) opts.push_back({int(s) * 2 + 1, "2 cruisers in " + sysName(int(s)), ""});
                }
                if (!opts.empty()) {
                    opts.push_back({-1, "Place nothing", ""});
                    curValue_ = kAbReductionPlace;
                    const int c = pick(t, Tag::AbsolChoice, "Arms Reduction: place 1 dreadnought or 2 cruisers where?", opts);
                    curValue_ = 0;
                    if (c >= 0) {
                        const int s = c / 2;
                        if (c % 2) addUnits(t, UnitType::Cruiser, std::min(2, unitsForPlacement(G, t, UnitType::Cruiser)), s, -1);
                        else addUnits(t, UnitType::Dreadnought, 1, s, -1);
                        say(t, pn(t) + (c % 2 ? " places cruisers in " : " places a dreadnought in ") + sysName(s) + " (Arms Reduction).", s);
                        enforceFleetLimit(t);
                    }
                }
            }
        }
    } else if (is("absol_secret")) {  // Archived Secret
        drawSecret(o);
        drawSecret(o);
        const int t = absolTop(a, o, votedFor, votes, o);
        if (t >= 0) absolGiveNote(o, t, "Archived Secret");
    } else if (is("absol_seeds")) {  // Seed of an Empire
        if (!against) {
            const int w = leastOrMost(true, "Seed of an Empire");
            absolSetSpeaker(w, "Seed of an Empire");
            gainVP(w, 1, "Seed of an Empire");
            for (int q : voters(0))
                if (q != w) absolGiveNote(w, q, "Seed of an Empire");
        } else {
            const int w = leastOrMost(false, "Seed of an Empire");
            gainVP(w, 1, "Seed of an Empire");
            drawActionCards(w, 3, "Seed of an Empire");
            addToPool(w, 0, 1, "Seed of an Empire");
            addToPool(w, 1, 1, "Seed of an Empire");
            addToPool(w, 2, 1, "Seed of an Empire");
        }
    } else if (is("absol_spending")) {  // Discretionary Spending
        if (!against) {
            for (int q = 0; q < n; ++q)
                if (!G.players[size_t(q)].eliminated) {
                    int k = 0;
                    for (int x : controlledPlanets(G, q))
                        if (!isStation(G, x)) ++k;
                    gainTG(q, k);
                }
            const int t = top(0);
            if (t >= 0) {
                Player& pl = G.players[size_t(t)];
                const int before = pl.commodities;
                pl.commodities = int8_t(std::max<int>(pl.commodities, commodityCap(G, pl)));
                say(t, pn(t) + " replenishes their commodities.");
                replenished(t, pl.commodities - before);
            }
            for (int q : clockwiseFrom(G, (G.speaker + 1) % n)) absolSpending(q);
        } else {
            const int t = top(1);
            for (int q = 0; q < n; ++q) {
                if (q == t || G.players[size_t(q)].eliminated) continue;
                Player& pl = G.players[size_t(q)];
                const int k = (pl.tradeGoods + 1) / 2;
                pl.tradeGoods = int8_t(pl.tradeGoods - k);
                if (k) say(q, pn(q) + " loses " + std::to_string(k) + " trade goods (Discretionary Spending).");
            }
        }
    } else if (is("absol_standardization")) {  // Armed Forces Standardization
        Player& pl = G.players[size_t(o)];
        const int total = kCommandTokens - tokensOnBoard(o) - edictTokensOf(G, o);
        pl.tactic = int8_t(std::min(3, total));
        pl.fleet = int8_t(std::min(3, total - pl.tactic));
        pl.strategy = int8_t(std::min(2, total - pl.tactic - pl.fleet));
        enforceFleetLimit(o);
        say(o, pn(o) + "'s command sheet is set to 3 / 3 / 2.");
        const int t = absolTop(a, o, votedFor, votes, o);
        if (t >= 0) absolGiveNote(t, o, "Armed Forces Standardization");
    } else if (is("absol_swords")) {  // Swords to Plowshares
        if (!against) {
            const int t = top(0);
            std::vector<int> fromPlanets;
            for (int q = 0; q < n; ++q) {
                if (G.players[size_t(q)].eliminated) continue;
                int lost = 0;
                for (int x : controlledPlanets(G, q)) {
                    const int inf = G.planets[size_t(x)].units[size_t(q)].count(UnitType::Infantry);
                    const int k = inf / 2;  // (half, rounding down)
                    if (!k) continue;
                    removing_ = true;
                    destroyUnits(q, UnitType::Infantry, k, G.planets[size_t(x)].system, x, -1, "Swords to Plowshares");
                    removing_ = false;
                    lost += k;
                    if (q == t) fromPlanets.push_back(x);
                }
                if (lost) gainTG(q, lost);
            }
            if (t >= 0 && !fromPlanets.empty()) {
                curValue_ = kAbSwordsExplore;
                const bool yes = yesNo(t, Tag::AbsolChoice, "Swords to Plowshares: explore every planet your infantry were removed from?",
                                       std::to_string(fromPlanets.size()) + " planets.");
                curValue_ = 0;
                if (yes)
                    for (int x : fromPlanets)
                        if (G.planets[size_t(x)].owner == t) explorePlanet(t, x);
            }
        } else {
            const int t = top(1);
            if (t >= 0)
                for (int x : controlledPlanets(G, t))
                    if (!isStation(G, x) && unitsForPlacement(G, t, UnitType::Infantry) > 0) addUnits(t, UnitType::Infantry, 1, G.planets[size_t(x)].system, x);
        }
    } else if (is("absol_whresearch")) {  // Wormhole Research
        if (!against) {
            std::vector<int> researched;
            for (int q : clockwiseFrom(G, G.speaker)) {
                if (G.players[size_t(q)].eliminated) continue;
                uint8_t types = 0;
                for (size_t s = 0; s < G.systems.size(); ++s)
                    if (hasShips(G, q, int(s))) types = uint8_t(types | wormholesOf(G, int(s)));
                int k = 0;
                for (int b = 0; b < 8; ++b)
                    if ((types >> b) & 1) ++k;
                if (k <= 0) continue;
                const std::vector<int16_t> before = G.players[size_t(q)].techs;
                optionalResearch_ = true;
                maxPrereqs_ = k;  // "a technology with no more than 1 prerequisite per type of wormhole in those systems"
                researchStep(q, 0, "Wormhole Research");
                maxPrereqs_ = -1;
                optionalResearch_ = false;
                for (int16_t x : G.players[size_t(q)].techs)
                    if (std::find(before.begin(), before.end(), x) == before.end() && kTechs[x].faction < 0) researched.push_back(x);
            }
            const int t = top(0);
            if (t >= 0) {
                std::vector<Option> opts;
                for (int x : researched)
                    if (!G.players[size_t(t)].hasTech(x) && techAvailableToResearch(G, t, x)) {
                        bool dup = false;
                        for (const Option& op : opts) dup = dup || op.id == x;
                        if (!dup) opts.push_back({x, kTechs[x].name, techSummary(x)});
                    }
                if (!opts.empty()) {
                    opts.push_back({-1, "Gain none", ""});
                    curValue_ = kAbWhGain;
                    const int x = pick(t, Tag::AbsolChoice, "Wormhole Research: gain one of the technologies researched this way?", opts);
                    curValue_ = 0;
                    if (x >= 0) {
                        researchTech(t, x, false);
                        E.log(t, pn(t) + " gains " + kTechs[x].name + ".", EventKind::Research);
                    }
                }
            }
        } else {
            // "In systems that contain their ships and wormholes, each player who voted For destroys 1 non-fighter ship for each alpha or
            // beta wormhole. Each player who voted Against or abstained removes 1 command token from their command sheet"
            for (int q : voters(0)) {
                for (size_t s = 0; s < G.systems.size(); ++s) {
                    const uint8_t w = wormholesOf(G, int(s));
                    const int k = ((w & kAlpha) ? 1 : 0) + ((w & kBeta) ? 1 : 0);
                    for (int i = 0; i < k; ++i) {
                        std::vector<Option> opts;
                        for (int u : kShipIdx)
                            if (u != int(UnitType::Fighter) && G.systems[s].space[size_t(q)].n[u])
                                opts.push_back({u, std::string(unitTypeName(UnitType(u))) + " in " + sysName(int(s)), ""});
                        if (opts.empty()) break;
                        curValue_ = kAbWhDestroy;
                        const int u = opts.size() == 1 ? opts[0].id : pick(q, Tag::AbsolChoice, "Wormhole Research: destroy which ship in " + sysName(int(s)) + "?", opts);
                        curValue_ = 0;
                        destroyUnits(q, UnitType(u), 1, int(s), -1, -1, "Wormhole Research");
                    }
                    removeExcessCapacity(q, int(s));
                }
            }
            for (int q : againstOrAbstained()) {
                Player& pl = G.players[size_t(q)];
                std::vector<Option> opts;
                if (pl.tactic) opts.push_back({0, "Tactic pool", std::to_string(pl.tactic) + " tokens"});
                if (pl.fleet) opts.push_back({1, "Fleet pool", std::to_string(pl.fleet) + " tokens"});
                if (pl.strategy) opts.push_back({2, "Strategy pool", std::to_string(pl.strategy) + " tokens"});
                if (opts.empty()) continue;
                const int c = opts.size() == 1 ? opts[0].id : pick(q, Tag::TokenFromPool, "Wormhole Research: remove a token from which pool?", opts);
                (c == 0 ? pl.tactic : c == 1 ? pl.fleet : pl.strategy) -= 1;
                enforceFleetLimit(q);
            }
            checkEliminations();
        }
    }
}

// Discretionary Spending (For): "In voting order, each player may spend any number of trade goods as follows ... (they cannot choose
// the same system or planet more than once)"
void Rules::absolSpending(int p) {
    Player& pl = G.players[size_t(p)];
    if (pl.eliminated) return;
    std::vector<int> usedSys, usedPlanet;
    int researched = 0;
    auto used = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    for (int guard = 0; guard < 30 && pl.tradeGoods > 0; ++guard) {
        std::vector<Option> opts;
        // production: the cost in trade goods
        bool prod = false;
        for (size_t s = 0; s < G.systems.size() && !prod; ++s)
            if (!used(usedSys, int(s)) && productionCapacity(G, p, int(s)) > 0 && !blockaded(G, p, int(s))) prod = true;
        if (prod) opts.push_back({1, "Use PRODUCTION in a system", "Pay its cost in trade goods."});
        if (researched < 2 && pl.tradeGoods >= 4) {
            bool any = false;
            for (int t = 0; t < kTechCount && !any; ++t)
                if (canResearchNow(G, p, t)) any = true;
            if (any) opts.push_back({2, "Research a technology (4)", std::to_string(2 - researched) + " left."});
        }
        if (pl.tradeGoods >= 3) {
            bool spot = false;
            for (int x : controlledPlanets(G, p))
                if (!used(usedPlanet, x) && !isStation(G, x) &&
                    ((unitsForPlacement(G, p, UnitType::PDS) > 0 && G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2) ||
                     (unitsForPlacement(G, p, UnitType::SpaceDock) > 0 && !G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock))))
                    spot = true;
            if (spot) opts.push_back({3, "Place a structure (3)", "A PDS or a space dock on a planet you control."});
        }
        if (!G.actionDeck.empty() || !G.actionDiscard.empty()) opts.push_back({4, "Draw an action card (1)", ""});
        {
            bool ex = false;
            for (int x : controlledPlanets(G, p))
                if (!used(usedPlanet, x) && !isStation(G, x) && explorable(G, x)) ex = true;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (!used(usedSys, int(s)) && G.systems[s].frontier && hasShips(G, p, int(s))) ex = true;
            if (ex) opts.push_back({5, "Explore (1)", "A planet you control, or a frontier token in a system with your ships."});
        }
        if (opts.empty()) break;
        opts.push_back({0, "Done spending", std::to_string(pl.tradeGoods) + " trade goods left."});
        curValue_ = kAbSpendMenu;
        const int c = pick(p, Tag::AbsolChoice, "Discretionary Spending: spend trade goods on what?", opts,
                           "You have " + std::to_string(pl.tradeGoods) + " trade goods.");
        curValue_ = 0;
        if (c == 0) break;
        if (c == 1) {
            std::vector<Option> so;
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (!used(usedSys, int(s)) && productionCapacity(G, p, int(s)) > 0 && !blockaded(G, p, int(s)))
                    so.push_back({int(s), sysName(int(s)), "PRODUCTION " + std::to_string(productionCapacity(G, p, int(s)))});
            so.push_back({-1, "Back", ""});
            curValue_ = kAbSpendSystem;
            const int s = pick(p, Tag::AbsolChoice, "Discretionary Spending: use PRODUCTION in which system?", so, "", true);
            curValue_ = 0;
            if (s < 0) continue;
            usedSys.push_back(s);
            tgOnly_ = true;  // (paid with trade goods)
            production(p, s, productionCapacity(G, p, s), false);
            tgOnly_ = false;
        } else if (c == 2) {
            pl.tradeGoods = int8_t(pl.tradeGoods - 4);
            ++researched;
            optionalResearch_ = true;
            const bool ok = researchStep(p, 0, "Discretionary Spending");
            optionalResearch_ = false;
            if (!ok) pl.tradeGoods = int8_t(pl.tradeGoods + 4);
        } else if (c == 3) {
            std::vector<Option> po;
            for (int x : controlledPlanets(G, p)) {
                if (used(usedPlanet, x) || isStation(G, x)) continue;
                if (unitsForPlacement(G, p, UnitType::PDS) > 0 && G.planets[size_t(x)].units[size_t(p)].count(UnitType::PDS) < 2)
                    po.push_back({x * 2, "PDS on " + planetName(x), ""});
                if (unitsForPlacement(G, p, UnitType::SpaceDock) > 0 && !G.planets[size_t(x)].units[size_t(p)].count(UnitType::SpaceDock))
                    po.push_back({x * 2 + 1, "Space dock on " + planetName(x), ""});
            }
            po.push_back({-1, "Back", ""});
            curValue_ = kAbSpendStructure;
            const int s = pick(p, Tag::AbsolChoice, "Discretionary Spending: place which structure?", po);
            curValue_ = 0;
            if (s < 0) continue;
            const int x = s / 2;
            const UnitType u = s % 2 ? UnitType::SpaceDock : UnitType::PDS;
            pl.tradeGoods = int8_t(pl.tradeGoods - 3);
            usedPlanet.push_back(x);
            addUnits(p, u, 1, G.planets[size_t(x)].system, x);
            say(p, pn(p) + " places a " + unitTypeName(u) + " on " + planetName(x) + " (Discretionary Spending).", G.planets[size_t(x)].system);
            if (u == UnitType::PDS) absolPdsPlaced(p, x);
        } else if (c == 4) {
            pl.tradeGoods = int8_t(pl.tradeGoods - 1);
            drawActionCards(p, 1, "Discretionary Spending");
        } else if (c == 5) {
            std::vector<Option> eo;
            for (int x : controlledPlanets(G, p))
                if (!used(usedPlanet, x) && !isStation(G, x) && explorable(G, x)) eo.push_back({x, planetName(x), ""});
            for (size_t s = 0; s < G.systems.size(); ++s)
                if (!used(usedSys, int(s)) && G.systems[s].frontier && hasShips(G, p, int(s)))
                    eo.push_back({100000 + int(s), "Frontier token in " + sysName(int(s)), ""});
            eo.push_back({-1, "Back", ""});
            curValue_ = kAbSpendExplore;
            const int e = pick(p, Tag::AbsolChoice, "Discretionary Spending: explore what?", eo);
            curValue_ = 0;
            if (e < 0) continue;
            pl.tradeGoods = int8_t(pl.tradeGoods - 1);
            if (e >= 100000) {
                usedSys.push_back(e - 100000);
                exploreFrontier(p, e - 100000);
            } else {
                usedPlanet.push_back(e);
                explorePlanet(p, e);
            }
        }
    }
}

// ------------------------------------------------------------------ laws leaving play
void Rules::absolLawLeaves(const Law& l, bool discarded) {
    const int a = l.agenda;
    if (kAgendas[a].source != Source::Absol) return;
    if (abKey(a, "absol_censure") && l.owner >= 0) loseVP(l.owner, 1, "lost Political Censure");
    if (abKey(a, "absol_travelban") || abKey(a, "absol_recon") || abKey(a, "absol_nexus")) computeAdjacency(G);
    if (abKey(a, "absol_leak") && l.target >= 0) {
        auto it = std::find(G.publicObjectives.begin(), G.publicObjectives.end(), l.target);
        if (it != G.publicObjectives.end()) G.publicObjectives.erase(it);
    }
    if (abKey(a, "absol_affirmation") && l.against && discarded) {
        // "If this law is discarded, the player who controls Mecatol Rex gains the custodians token and 1 victory point."
        const int mec = G.mecatol();
        int owner = -1;
        if (mec >= 0)
            for (int16_t x : G.systems[size_t(mec)].planets)
                if (G.planets[size_t(x)].owner >= 0) owner = G.planets[size_t(x)].owner;
        if (owner >= 0) {
            G.custodiansBy = int8_t(owner);
            gainVP(owner, 1, "Affirmation of Divine Right: the custodians token");
        }
    }
}

// ------------------------------------------------------------------ the agenda phase
void Rules::absolAgendaPhaseStart() {
    if (!absolOn()) return;
    absolSkip_ = false;
    // Judicial Abolishment (For): "At the start of the Agenda Phase, the speaker may spend 6 influence to discard any 1 law in play."
    if (!lawByKey(G, "absol_abolishment") || spendable(G, G.speaker, true) < 6) return;
    std::vector<Option> opts;
    for (const Law& l : G.laws) opts.push_back({l.agenda, kAgendas[l.agenda].name, agendaText(l.agenda, l.against)});
    opts.push_back({-1, "Discard nothing", ""});
    curValue_ = kAbAbolishLaw;
    curAgenda_ = findAgenda("absol_abolishment");
    const int law = pick(G.speaker, Tag::AbsolChoice, "Judicial Abolishment: spend 6 influence to discard a law?", opts);
    curAgenda_ = -1;
    curValue_ = 0;
    if (law < 0 || !pay(G.speaker, 6, true, "Judicial Abolishment", true)) return;
    for (size_t i = 0; i < G.laws.size(); ++i)
        if (G.laws[i].agenda == law) {
            discardLaw(int(i), "Judicial Abolishment");
            break;
        }
}

// Political Secret (0.81.0, owner 2026-11-21: Absol's card replaces the normal one in games with Absol's Agendas): "When you cast
// votes: You may exhaust up to 3 of the <color> player's planets and cast votes as if you controlled the exhausted planets. Then
// return this card to the <color> player."
bool Rules::absolPoliticalSecretUsable(int p) const {
    if (!absolOn() || G.fixes < 36) return false;
    for (int16_t note : G.players[size_t(p)].notes) {
        const int o = noteOwner(note);
        if (noteType(note) == kNotePoliticalSecret && o != p && o >= 0 && !G.players[size_t(o)].eliminated && !ti::readyPlanets(G, o).empty())
            return true;
    }
    return false;
}

int Rules::absolPoliticalSecret(int p, bool exec, bool absolGov) {
    int total = 0;
    for (int16_t note : std::vector<int16_t>(G.players[size_t(p)].notes)) {
        const int o = noteOwner(note);
        if (noteType(note) != kNotePoliticalSecret || o == p || o < 0 || G.players[size_t(o)].eliminated) continue;
        auto votesOf = [&](int x) {  // "as if you controlled" it
            return exec       ? std::max(voteValue(G, p, x), planetResources(G, x))
                   : absolGov ? 1 + (G.planets[size_t(x)].system == G.mecatol() && !std::strcmp(kPlanets[G.planets[size_t(x)].def].key, "mr") ? 1 : 0)
                              : voteValue(G, p, x);
        };
        std::vector<Option> opts;
        for (int x : ti::readyPlanets(G, o)) opts.push_back({x, planetName(x), std::to_string(votesOf(x)) + (votesOf(x) == 1 ? " vote" : " votes")});
        if (opts.empty()) continue;
        curValue_ = kAbPoliticalSecret;
        const std::vector<int> picks = pickMany(p, Tag::AbsolChoice, "Political Secret (" + pn(o) + "): exhaust up to 3 of their planets to cast votes?", opts, 0,
                                                3, noteTextOf(G, note), true);
        curValue_ = 0;
        if (picks.empty()) continue;
        int v = 0;
        std::string names;
        for (int x : picks) {
            if (G.planets[size_t(x)].exhausted || G.planets[size_t(x)].owner != o) continue;
            G.planets[size_t(x)].exhausted = true;
            v += votesOf(x);
            names += (names.empty() ? "" : ", ") + planetName(x);
        }
        say(p, pn(p) + " plays " + pn(o) + "'s Political Secret: exhausts " + names + " for " + std::to_string(v) + (v == 1 ? " vote." : " votes."));
        returnNote(p, note);
        total += v;
    }
    return total;
}

void Rules::absolEmergencyDone() {
    if (emergencyCards_.empty()) return;
    G.agendaDiscard.push_back(emergencyCards_.front());  // "Then discard this card."
    emergencyCards_.erase(emergencyCards_.begin());
}

void Rules::absolAgendaPhaseEnd() {
    if (!absolOn()) return;
    // Absol FAQ (Pax Magnifica, Bellum Gloriosum): "the timing window for Emergency Session is skipped. The Emergency Session cards
    // remain in the common play area and are resolved during the next agenda phase."
    if (!absolSkip_) {
        for (int16_t c : emergencyCards_) G.agendaDiscard.push_back(c);
        emergencyCards_.clear();
        G.emergency = 0;
    } else if (!emergencyCards_.empty()) {
        say(-1, std::to_string(emergencyCards_.size()) + " Emergency Session card(s) stay in the common play area for the next agenda phase.");
    }
    absolSkip_ = false;
}

// ------------------------------------------------------------------ the strategy phase
void Rules::absolPending(const PendingEffect& pe, int q) {
    const char* k = kAgendas[pe.agenda].key;
    if (!std::strcmp(k, "absol_conscription")) {  // Against: "exhaust all of their industrial planets"
        for (int x : controlledPlanets(G, q))
            if (hasTraitOf(G, x, Trait::Industrial)) G.planets[size_t(x)].exhausted = true;
    } else if (!std::strcmp(k, "absol_government")) {  // Against: cultural planets, and no Politics
        for (int x : controlledPlanets(G, q))
            if (hasTraitOf(G, x, Trait::Cultural)) G.planets[size_t(x)].exhausted = true;
        absolNoPolitics_ = uint8_t(absolNoPolitics_ | (1u << q));
    }
}

bool Rules::absolCardLocked(int p, int card) const {
    if (!absolOn()) return false;
    if ((G.scLocked >> card) & 1) return true;
    if (card == 2 && ((absolNoPolitics_ >> p) & 1)) {  // "unless it is the only unchosen strategy card"
        int left = 0;
        for (int c = 0; c < kStrategyCards; ++c)
            if (G.cardOwner[c] < 0 && !((G.scLocked >> c) & 1)) ++left;
        return left > 1;
    }
    return false;
}

bool Rules::absolChecksKeep(int p, int card) {
    Law* l = lawByKeyMut(G, "absol_checks");
    if (!l || G.lawsBlank || !l->tokens[size_t(p)] || !G.players[size_t(p)].cards.empty()) return false;
    curValue_ = kAbChecksKeep;
    curAgenda_ = l->agenda;
    const bool keep = yesNo(p, Tag::AbsolChoice, std::string("Checks and Balances: discard your control token to keep ") + strategyName(card) + "?",
                            "Otherwise you give it to another player.");
    curAgenda_ = -1;
    curValue_ = 0;
    if (!keep) return false;
    l->tokens[size_t(p)] = 0;
    say(p, pn(p) + " discards their control token from Checks and Balances to keep " + strategyName(card) + ".");
    return true;
}

void Rules::absolStrategyEnd() {
    G.scLocked = 0;
    absolNoPolitics_ = 0;
    if (!absolOn()) return;
    // Imperial Arbiter (Absol): "spend 1 command token from their strategy pool and up to 8 influence to swap 1 of their strategy cards
    // with a strategy card that has an initiative value equal to or less than the influence spent" (RULES-CHECK AB3: another player's card)
    if (const Law* l = lawByKey(G, "absol_arbiter")) {
        const int arb = l->owner;
        if (arb >= 0 && !G.players[size_t(arb)].eliminated && !G.players[size_t(arb)].cards.empty() && G.players[size_t(arb)].strategy > 0) {
            const int inf = std::min(8, spendable(G, arb, true));
            std::vector<Option> opts;
            for (int8_t mine : G.players[size_t(arb)].cards)
                for (int q = 0; q < G.numPlayers; ++q)
                    if (q != arb)
                        for (int8_t theirs : G.players[size_t(q)].cards)
                            if (theirs + 1 <= inf)
                                opts.push_back({mine * 100 + theirs,
                                                std::string("Swap your ") + strategyName(mine) + " for " + pn(q) + "'s " + strategyName(theirs),
                                                std::to_string(theirs + 1) + " influence and a strategy token"});
            if (!opts.empty()) {
                opts.push_back({-1, "Don't swap", ""});
                curValue_ = kAbArbiter;
                curAgenda_ = l->agenda;
                const int c = pick(arb, Tag::AbsolChoice, "Imperial Arbiter: swap strategy cards?", opts);
                curAgenda_ = -1;
                curValue_ = 0;
                if (c >= 0) {
                    const int mine = c / 100, theirs = c % 100, q = G.cardOwner[theirs];
                    if (pay(arb, theirs + 1, true, "Imperial Arbiter", true)) {
                        spendStrategy(arb, 1);
                        auto& x = G.players[size_t(arb)].cards;
                        auto& y = G.players[size_t(q)].cards;
                        *std::find(x.begin(), x.end(), int8_t(mine)) = int8_t(theirs);
                        *std::find(y.begin(), y.end(), int8_t(theirs)) = int8_t(mine);
                        G.cardOwner[mine] = int8_t(q);
                        G.cardOwner[theirs] = int8_t(arb);
                        say(arb, pn(arb) + " swaps " + strategyName(mine) + " for " + pn(q) + "'s " + strategyName(theirs) + " (Imperial Arbiter).");
                    }
                }
            }
        }
    }
    // Affirmation of Divine Right (For): "At the end of each strategy phase, each other player must give the player who controls Mecatol
    // Rex 1 promissory note from their hand, if able."
    if (const Law* l = lawByKey(G, "absol_affirmation")) {
        if (!l->against) {
            const int mec = G.mecatol();
            int owner = -1;
            if (mec >= 0)
                for (int16_t x : G.systems[size_t(mec)].planets)
                    if (G.planets[size_t(x)].owner >= 0) owner = G.planets[size_t(x)].owner;
            if (owner >= 0) {
                curAgenda_ = l->agenda;
                for (int q : clockwiseFrom(G, G.speaker))
                    if (q != owner) absolGiveNote(q, owner, "Affirmation of Divine Right");
                curAgenda_ = -1;
            }
        }
    }
}

bool Rules::absolMinisterFree(int p, int card) const {
    if (!absolOn()) return false;
    static const char* const kMin[kStrategyCards] = {nullptr, "absol_minspeace", "absol_minspolicy", "absol_minsindus",
                                                     "absol_minscomm", "absol_minswar", "absol_minssci", "absol_minsantique"};
    if (card < 0 || card >= kStrategyCards || !kMin[card]) return false;
    const Law* l = lawByKey(G, kMin[card]);
    return l && l->owner == p;
}

// ------------------------------------------------------------------ the action phase
bool Rules::absolActivated(int p, int sys) {
    if (!absolOn()) return false;
    // Articles of War (For): "When any player activates a system, a player whose control token is on this card may discard 1 control
    // token to ignore the effects of this law during that tactical action."
    if (Law* l = lawByKeyMut(G, "absol_articleswar"); l && !G.lawsBlank) {
        bool mechs = false;  // (asked only when a mech is in or next to the system - otherwise the law changes nothing)
        std::vector<int> near{sys};
        for (int16_t x : G.systems[size_t(sys)].adj) near.push_back(x);
        for (int s : near) {
            for (int q = 0; q < G.numPlayers; ++q) {
                if (G.systems[size_t(s)].space[size_t(q)].count(UnitType::Mech)) mechs = true;
                for (int16_t x : G.systems[size_t(s)].planets)
                    if (G.planets[size_t(x)].units[size_t(q)].count(UnitType::Mech)) mechs = true;
            }
        }
        if (mechs)
            for (int q : clockwiseFrom(G, p)) {
                if (!l->tokens[size_t(q)] || G.players[size_t(q)].eliminated) continue;
                curValue_ = kAbArticlesIgnore;
                curAgenda_ = l->agenda;
                const bool use = yesNo(q, Tag::AbsolChoice, "Articles of War: discard a control token to ignore the law during this tactical action?",
                                       pn(p) + " activated " + sysName(sys) + ". Mechs keep their printed abilities until it ends.");
                curAgenda_ = -1;
                curValue_ = 0;
                if (use) {
                    --l->tokens[size_t(q)];
                    G.absolIgnore = uint8_t(G.absolIgnore | 1);
                    say(q, pn(q) + " discards a control token: Articles of War is ignored during this tactical action.");
                    break;
                }
            }
    }
    // Minister of Peace (Absol): "After a player activates a system that contains 1 or more of another player's units, the owner of this card
    // may spend 1 command token from their strategy pool and give the active player 1 promissory note from their hand to immediately end
    // that player's turn."
    if (const Law* l = lawByKey(G, "absol_minspeace")) {
        const int peace = l->owner;
        if (peace >= 0 && peace != p && !G.players[size_t(peace)].eliminated && G.players[size_t(peace)].strategy > 0 &&
            !G.players[size_t(peace)].notes.empty() && peaceQuiet_[peace] != G.round) {
            bool others = false;
            for (int q = 0; q < G.numPlayers; ++q)
                if (q != p && hasUnits(G, q, sys)) others = true;
            if (others) {
                curValue_ = kAbPeace;
                curAgenda_ = l->agenda;
                int c;
                const std::string q = "Minister of Peace: spend a strategy token and give " + pn(p) + " a promissory note to end their turn?",
                                  ctx = pn(p) + " activated " + sysName(sys) + ".";
                if (G.players[size_t(peace)].human)
                    c = pick(peace, Tag::AbsolChoice, q,
                             {{1, "Yes", ""}, {0, "No", ""}, {3, "No, and don't ask again this round", "You'll be asked again next round."}}, ctx);
                else
                    c = yesNo(peace, Tag::AbsolChoice, q, ctx) ? 1 : 0;
                curAgenda_ = -1;
                curValue_ = 0;
                if (c == 3) peaceQuiet_[peace] = int16_t(G.round);
                if (c == 1) {
                    spendStrategy(peace, 1);
                    absolGiveNote(peace, p, "Minister of Peace");
                    say(peace, pn(peace) + " uses the Minister of Peace: " + pn(p) + "'s turn ends.");
                    return true;
                }
            }
        }
    }
    return false;
}

void Rules::absolTurnStart(int p) {
    if (!absolOn()) return;
    G.absolIgnore = 0;
    // Enforced Travel Ban (For): "A player whose control token is on this card may discard 1 control token at any time to allow the current
    // active player to ignore the effects of this law for the duration of their turn." (RULES-CHECK AB4: offered at the start of a turn)
    if (Law* l = lawByKeyMut(G, "absol_travelban"); l && !G.lawsBlank) {
        for (int q : clockwiseFrom(G, p)) {
            if (!l->tokens[size_t(q)] || G.players[size_t(q)].eliminated) continue;
            curValue_ = kAbTravelIgnore;
            curAgenda_ = l->agenda;
            curFor_ = p;
            const bool use = yesNo(q, Tag::AbsolChoice, "Enforced Travel Ban: discard a control token so " + (q == p ? std::string("you") : pn(p)) +
                                                            " may use alpha and beta wormholes this turn?");
            curFor_ = -1;
            curAgenda_ = -1;
            curValue_ = 0;
            if (use) {
                --l->tokens[size_t(q)];
                G.absolIgnore = uint8_t(G.absolIgnore | 2);
                say(q, pn(q) + " discards a control token: " + pn(p) + " ignores Enforced Travel Ban this turn.");
                computeAdjacency(G);
                break;
            }
        }
    }
    // Nexus Sovereignty (For): "The player who controls Mallice may spend 1 influence at any time to allow the current active player to
    // ignore the effects of this law for the duration of their turn." (offered at the start of a turn)
    if (const Law* l = lawByKey(G, "absol_nexus")) {
        int mallice = -1;
        if (G.nexus >= 0)
            for (int16_t x : G.systems[size_t(G.nexus)].planets)
                if (G.planets[size_t(x)].owner >= 0) mallice = G.planets[size_t(x)].owner;
        if (mallice >= 0 && !G.players[size_t(mallice)].eliminated && spendable(G, mallice, true) >= 1) {
            curValue_ = kAbNexusIgnore;
            curAgenda_ = l->agenda;
            curFor_ = p;
            const bool use = yesNo(mallice, Tag::AbsolChoice, "Nexus Sovereignty: spend 1 influence so " + (mallice == p ? std::string("you") : pn(p)) +
                                                                  " may move through the nexus's wormholes this turn?");
            curFor_ = -1;
            curAgenda_ = -1;
            curValue_ = 0;
            if (use && pay(mallice, 1, true, "Nexus Sovereignty", true)) {
                G.absolIgnore = uint8_t(G.absolIgnore | 4);
                say(mallice, pn(mallice) + " spends 1 influence: " + pn(p) + " ignores Nexus Sovereignty this turn.");
                computeAdjacency(G);
            }
        }
    }
}

void Rules::absolTurnEnd(int p) {
    (void)p;
    if (!absolOn() || !G.absolIgnore) return;
    const bool adj = (G.absolIgnore & 6) != 0;
    G.absolIgnore = 0;
    if (adj) computeAdjacency(G);
}

void Rules::absolAfterTactical(int p, int sys) {
    if (!absolOn()) return;
    G.absolIgnore = uint8_t(G.absolIgnore & ~1u);  // (Articles of War: only during that tactical action)
    // Minister of Exploration (Absol): "After performing a tactical action, the owner of this card may explore 1 frontier token or 1 planet that
    // contains their units in the active system."
    const Law* l = lawByKey(G, "absol_minsexp");
    if (!l || l->owner != p || sys < 0 || G.players[size_t(p)].eliminated) return;
    std::vector<Option> opts;
    if (G.systems[size_t(sys)].frontier) opts.push_back({100000 + sys, "The frontier token", ""});
    for (int16_t x : G.systems[size_t(sys)].planets)
        if (G.planets[size_t(x)].units[size_t(p)].total() > 0 && explorable(G, x) && !isStation(G, x))
            opts.push_back({x, planetName(x), ""});
    if (opts.empty()) return;
    opts.push_back({-1, "Don't explore", ""});
    curValue_ = kAbExploration;
    curAgenda_ = l->agenda;
    const int e = pick(p, Tag::AbsolChoice, "Minister of Exploration: explore in " + sysName(sys) + "?", opts);
    curAgenda_ = -1;
    curValue_ = 0;
    if (e < 0) return;
    if (e >= 100000) exploreFrontier(p, e - 100000);
    else explorePlanet(p, e);
}

void Rules::absolOptions(int p, std::vector<Option>& opts) {
    if (!absolOn()) return;
    // Minister of War (Absol): "ACTION: Exhaust this card and spend 1 command token from your strategy pool to remove 1 of your command
    // tokens from the game board."
    const Law* l = lawByKey(G, "absol_minswar");
    if (l && l->owner == p && !l->exhausted && G.players[size_t(p)].strategy > 0 && tokensOnBoard(p) > 0)
        opts.push_back({3020, "Law: Minister of War", "Exhaust it and spend 1 strategy token to remove 1 of your command tokens from the board."});
}

bool Rules::absolMinisterWar(int p) {
    Law* l = lawByKeyMut(G, "absol_minswar");
    if (!l || l->owner != p || l->exhausted) return false;
    std::vector<Option> opts;
    for (size_t s = 0; s < G.systems.size(); ++s)
        if (G.systems[s].hasToken(p)) opts.push_back({int(s), sysName(int(s)), ""});
    if (opts.empty() || G.players[size_t(p)].strategy <= 0) return false;
    curValue_ = kAbWar;
    curAgenda_ = l->agenda;
    const int s = pick(p, Tag::AbsolChoice, "Minister of War: remove your command token from which system?", opts, "", true);
    curAgenda_ = -1;
    curValue_ = 0;
    l->exhausted = true;
    spendStrategy(p, 1);
    G.systems[size_t(s)].tokens &= uint16_t(~(1u << p));
    say(p, pn(p) + " exhausts the Minister of War and removes their command token from " + sysName(s) + ".", s);
    return true;
}

void Rules::absolProduced(int p, int sys, const int counts[kUnitTypes]) {
    (void)sys;
    if (!absolOn()) return;
    // Prophecy of Ixth (Absol): "When the owner of this card uses PRODUCTION, they return this card to the common play area unless they produce
    // 2 or more fighters. If this card is in the common play area, a player may take it if they produce 4 or more fighters when using PRODUCTION."
    Law* l = lawByKeyMut(G, "absol_prophecy");
    if (!l || G.lawsBlank) return;
    const int f = counts[int(UnitType::Fighter)];
    if (l->owner == p && f < 2) {
        l->owner = -1;
        say(p, pn(p) + " returns Prophecy of Ixth to the common play area (fewer than 2 fighters produced).");
    } else if (l->owner < 0 && f >= 4) {
        curValue_ = kAbProphecyTake;
        curAgenda_ = l->agenda;
        const bool take = yesNo(p, Tag::AbsolChoice, "Take Prophecy of Ixth from the common play area?", agendaText(l->agenda, false));
        curAgenda_ = -1;
        curValue_ = 0;
        if (take) {
            l->owner = int8_t(p);
            say(p, pn(p) + " takes Prophecy of Ixth (4 or more fighters produced).");
        }
    }
}

void Rules::absolPdsPlaced(int p, int planet) {
    if (!absolOn() || planet < 0 || !lawByKey(G, "absol_defenseact")) return;
    // Homeland Defense Act (For): "When placing 1 or more PDS on a planet, players may place an additional PDS on that planet." (RULES-CHECK
    // AB5: the 2-per-planet limit still applies)
    if (unitsForPlacement(G, p, UnitType::PDS) <= 0 || G.planets[size_t(planet)].units[size_t(p)].count(UnitType::PDS) >= 2) return;
    curValue_ = kAbDefenseExtra;
    curAgenda_ = findAgenda("absol_defenseact");
    const bool yes = yesNo(p, Tag::AbsolChoice, "Homeland Defense Act: place an additional PDS on " + planetName(planet) + "?");
    curAgenda_ = -1;
    curValue_ = 0;
    if (!yes) return;
    addUnits(p, UnitType::PDS, 1, G.planets[size_t(planet)].system, planet);
    say(p, pn(p) + " places an additional PDS on " + planetName(planet) + " (Homeland Defense Act).", G.planets[size_t(planet)].system);
}

bool Rules::absolExplorePay(int p, int planet) {
    if (!absolOn() || !lawByKey(G, "absol_sites")) return true;
    // Preservation of Historical Sites (For): "Players must spend 1 influence to explore industrial planets and 2 influence to explore cultural
    // planets." (hazardous planets are free; a planet with both traits costs the higher - RULES-CHECK AB6)
    int cost = 0;
    if (hasTraitOf(G, planet, Trait::Industrial)) cost = 1;
    if (hasTraitOf(G, planet, Trait::Cultural)) cost = 2;
    if (cost == 0) return true;
    if (spendable(G, p, true) < cost) {
        say(p, pn(p) + " can't spend " + std::to_string(cost) + " influence to explore " + planetName(planet) + " (Preservation of Historical Sites).");
        return false;
    }
    curValue_ = kAbSitesExplorePay;
    curAgenda_ = findAgenda("absol_sites");
    const bool yes = yesNo(p, Tag::AbsolChoice, "Preservation of Historical Sites: spend " + std::to_string(cost) + " influence to explore " + planetName(planet) + "?");
    curAgenda_ = -1;
    curValue_ = 0;
    return yes && pay(p, cost, true, "Preservation of Historical Sites", true);
}

bool Rules::absolScoreInstead(int p, int obj) {
    if (!absolOn() || G.relicDeck.empty()) return false;
    const Law* l = lawByKey(G, "absol_minsantique");
    if (!l || l->owner != p || (obj >= 0 && kObjectives[obj].kind == 3)) return false;
    // Minister of Antiquities (Absol): "Whenever the owner of this card may score a public objective, they may choose to draw a relic instead."
    // (obj -1: a chance to score with no objective met - the FAQ: they may still draw the relic)
    curValue_ = obj >= 0 ? kAbAntiquities : kAbAntiquitiesFree;
    curAgenda_ = l->agenda;
    const bool relic = yesNo(p, Tag::AbsolChoice, obj >= 0 ? std::string("Minister of Antiquities: draw a relic instead of scoring ") + kObjectives[obj].name + "?"
                                                         : std::string("Minister of Antiquities: draw a relic? (you may score a public objective now)"));
    curAgenda_ = -1;
    curValue_ = 0;
    if (!relic) return false;
    gainRelic(p, "Minister of Antiquities");
    return true;
}

void Rules::absolStatusDraws(std::vector<int>& draws) {
    if (!absolOn()) return;
    // Minister of Policy (Absol): "During the draw action cards step of the status phase, instead of players drawing randomly, the owner of this
    // card looks at a number of action cards from the top of the deck equal to the number of players plus 1, adds 2 to their hand, then gives
    // one to each other player. Players that are able to draw more than 1 action card may then draw additional cards, as normal."
    const Law* l = lawByKey(G, "absol_minspolicy");
    if (!l || l->owner < 0 || G.players[size_t(l->owner)].eliminated) return;
    const int o = l->owner, n = G.numPlayers;
    int alive = 0;
    for (int q = 0; q < n; ++q)
        if (!G.players[size_t(q)].eliminated) ++alive;
    std::vector<int16_t> look;
    for (int i = 0; i < alive + 1; ++i) {
        if (G.actionDeck.empty()) {
            if (G.actionDiscard.empty()) break;
            G.actionDeck = G.actionDiscard;
            G.actionDiscard.clear();
            E.rng().shuffle(G.actionDeck);
        }
        look.push_back(G.actionDeck.back());
        G.actionDeck.pop_back();
    }
    E.revealed();
    std::vector<Option> opts;
    for (size_t i = 0; i < look.size(); ++i) opts.push_back({int(i), kActionCards[look[i]].name, actionCardText(look[i])});
    curValue_ = kAbPolicyKeep;
    curAgenda_ = l->agenda;
    const int keepN = std::min<int>(2, int(look.size()));
    std::vector<int> keep = keepN > 0 ? pickMany(o, Tag::AbsolChoice, "Minister of Policy: add which 2 action cards to your hand?", opts, keepN, keepN)
                                      : std::vector<int>();
    std::vector<int16_t> rest;
    for (size_t i = 0; i < look.size(); ++i) {
        if (std::find(keep.begin(), keep.end(), int(i)) != keep.end()) G.players[size_t(o)].actionCards.push_back(look[i]);
        else rest.push_back(look[i]);
    }
    say(o, pn(o) + " adds 2 action cards to their hand (Minister of Policy).");
    for (int q : clockwiseFrom(G, (o + 1) % n)) {
        if (q == o || G.players[size_t(q)].eliminated || rest.empty()) continue;
        std::vector<Option> ro;
        for (size_t i = 0; i < rest.size(); ++i) ro.push_back({int(i), kActionCards[rest[i]].name, actionCardText(rest[i])});
        curValue_ = kAbPolicyGive;
        curFor_ = q;
        const int i = ro.size() == 1 ? 0 : pick(o, Tag::AbsolChoice, "Minister of Policy: give which action card to " + pn(q) + "?", ro);
        curFor_ = -1;
        G.players[size_t(q)].actionCards.push_back(rest[size_t(i)]);
        say(o, pn(o) + " gives " + pn(q) + " an action card (Minister of Policy).");
        rest.erase(rest.begin() + i);
        if (q < int(draws.size()) && draws[size_t(q)] > 0) --draws[size_t(q)];
    }
    for (int16_t c : rest) G.actionDiscard.push_back(c);
    curAgenda_ = -1;
    curValue_ = 0;
    if (o < int(draws.size()) && draws[size_t(o)] > 0) --draws[size_t(o)];
}

void Rules::absolSecretDiscarded(int p, int obj) {
    if (!absolOn()) return;
    // Search Warrant (Absol): "When the owner of this card would discard a secret objective, a player whose control token is on this card may
    // take that secret objective instead."
    const Law* l = lawByKey(G, "absol_warrant");
    if (!l || l->owner != p) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !l->tokens[size_t(q)] || G.players[size_t(q)].eliminated) continue;
        curValue_ = kAbWarrantTake;
        curAgenda_ = l->agenda;
        const bool take = yesNo(q, Tag::AbsolChoice, std::string("Search Warrant: take ") + pn(p) + "'s discarded secret objective " + kObjectives[obj].name + "?",
                                objectiveSummary(obj));
        curAgenda_ = -1;
        curValue_ = 0;
        if (!take) continue;
        auto& d = G.secretDeck;
        auto it = std::find(d.begin(), d.end(), int16_t(obj));
        if (it != d.end()) d.erase(it);
        G.players[size_t(q)].secrets.push_back(int16_t(obj));
        say(q, pn(q) + " takes " + pn(p) + "'s discarded secret objective (Search Warrant).");
        return;
    }
}

void Rules::absolControlChanged(int planet, int oldOwner, int newOwner) {
    if (!absolOn() || !G.mecArtifact || oldOwner == newOwner) return;
    const int mec = G.mecatol();
    if (mec < 0 || G.planets[size_t(planet)].system != mec) return;
    // Ixthian Artifact (Against): the planet card's point moves with control of Mecatol Rex (RULES-CHECK AB2)
    if (oldOwner >= 0 && !G.players[size_t(oldOwner)].eliminated) loseVP(oldOwner, 1, "lost control of Mecatol Rex (Ixthian Artifact)");
    if (newOwner >= 0) gainVP(newOwner, 1, "Ixthian Artifact");
}

void Rules::absolStatusEnd() {
    if (!absolOn()) return;
    for (Law& l : G.laws) l.exhausted = false;  // (Minister of War readies with the other cards)
}

void Rules::absolMovedThrough(int p) {
    if (!absolOn()) return;
    // Wormhole Reconstruction (For): "A player whose control token is on this card gains 1 commodity each time another player's ships move
    // through an alpha or beta wormhole." (once for each movement in which ships came through a wormhole - RULES-CHECK AB10)
    const Law* l = lawByKey(G, "absol_recon");
    if (!l) return;
    for (int q = 0; q < G.numPlayers; ++q) {
        if (q == p || !l->tokens[size_t(q)] || G.players[size_t(q)].eliminated) continue;
        Player& pl = G.players[size_t(q)];
        const int before = pl.commodities;
        if (pl.commodities < commodityCap(G, pl)) {
            ++pl.commodities;
            say(q, pn(q) + " gains 1 commodity (Wormhole Reconstruction).");
            commodityGained(q, before);
        }
    }
}

}  // namespace ti
