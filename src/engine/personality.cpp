#include "engine/personality.h"

#include <cstring>

#include "engine/deal.h"
#include "engine/query.h"

namespace ti {

// key, name, blurb, Tra Cea Sup All Pol, commodity, agent, faction-note multiplier, B-Factor start / gain
const ArchDef kArchetypes[int(Arch::Count)] = {
    {"politician", "Politician", "Balanced; strong in agendas; fair, persistent deals", 1, 2, 4, 2, 1, 1, 1, 1.0f, 20, 0.5f},
    {"peacekeeper", "Peacekeeper", "Seeks ceasefires and alliances; defends its partners", 1, 2, 3, 1, 3, 1, 1, 1.0f, 10, 0.8f},
    {"warlord", "Warlord", "Hyper-aggressive; big fleets; hard to deal with", 2, 3, 8, 3, 4, 2, 2, 2.0f, 25, 0.3f},
    {"betrayer", "Betrayer", "Makes alliances it means to break", 0, 3, 3, 1, 0, 1, 2, 0.7f, 55, 0.0f},
    {"swindler", "Swindler", "Self-preserving; promises freely, keeps some", 2, 1, 1, 2, 1, 1, 3, 0.7f, 35, 0.2f},
    {"merchant", "Merchant", "Trade and economy first; uses leverage", 0, 3, 5, 1, 2, 1, 1, 1.1f, 20, 0.5f},
    {"opportunist", "Opportunist", "Kind to the weak, hard on whoever blocks it", 2, 2, 2, 2, 2, 2, 2, 1.0f, 20, 0.4f},
    {"rusher", "Rusher", "Races for Mecatol Rex and the Imperial card", 3, 0, 2, 1, 5, 1, 2, 1.1f, 0, 0.7f},
    {"turtler", "Turtler", "Fortifies its slice and rarely leaves it", 1, 0, 4, 1, 3, 1, 6, 0.9f, 10, 0.8f},
    {"chaotic", "Chaotic", "Becomes a different archetype every round", 1, 2, 4, 2, 1, 1, 1, 1.0f, 45, 0.6f},
};

const NamedText kEvolutions[int(Evo::Count)] = {
    {"Late Bloomer", "Quiet and economic in rounds 1-3, then more aggressive and demanding every round."},
    {"Vengeful", "Remembers attacks: its grudge grows 1.5x faster and it hunts whoever hurt it most."},
    {"Mirror", "Treats each player the way they treat it."},
    {"Kingslayer", "From round 4 the points leader is its main enemy."},
    {"Ambitious", "Within 3 points of winning it drops its promises and goes all in."},
    {"Learner", "Raises its prices after refusals and lowers them after gifts."},
    {"Rising Diplomat", "Each accepted deal makes it easier to deal with; refusals make it harder."},
    {"Border Watch", "Turns defensive and offers ceasefires when fleets gather at its borders."},
    {"Scavenger", "Pounces on a neighbour that just lost a big fight."},
    {"Three Acts", "Expands (rounds 1-2), consolidates (rounds 3-4), then conquers."},
};

const NamedText kQuirks[int(Quirk::Count)] = {
    {"Reckless", "Goes through anomalies more often and takes riskier fights."},
    {"Holy Diver", "Attacks Mecatol Rex more often."},
    {"Chess Master", "Moves units into position a turn ahead for attacks and objectives."},
    {"Kneecapper", "Goes after the home system of a neighbour who leads it by 2+ points."},
    {"Tech Hoarder", "Researches more and values the Technology card and research deals."},
    {"Collector", "Chases relic fragments and frontier systems; pays more for fragments."},
    {"Grudge Keeper", "Never forgives an attack: its grudge never cools down."},
    {"Bargain Hunter", "Opens with low offers and wants more before it says yes."},
    {"Showboat", "Puts its flagship and war suns on the front line; loves combat objectives."},
    {"Cautious Admiral", "Attacks only with clearly better odds and retreats sooner."},
    {"Spoiler", "In agendas, votes for whatever hurts the points leader most."},
    {"Homebody", "Keeps an extra ship at home and rarely sends its fleet far."},
    {"Gambler", "Takes long-shot attacks and agenda bets more often."},
};

const NamedText kBetrayals[int(Betrayal::Count)] = {
    {"Break a deal", "Break a promise or deal with them."},
    {"Pass their note on", "Trade their promissory note to someone else."},
    {"Shake them down", "Threaten them for trade goods."},
    {"Hit the weak spot", "Attack their system with the fewest units."},
    {"Embargo", "Park ships in one of their systems."},
    {"Vote them down", "Vote against them on the next agenda."},
    {"Cold shoulder", "Refuse every offer from them for 2 rounds."},
    {"Land grab", "Take one of their planets."},
    {"Bad debt", "Borrow from them and don't pay it back."},
    {"Rumour", "Turn another player against them."},
    {"Kingslay", "Take Mecatol Rex from whoever has held it too long."},
};

namespace {
struct FQ {
    const char* faction;
    NamedText a, b;
};
const FQ kFactionQuirks[] = {
    {"arborec", {"Overgrowth", "Floods the board with infantry and mechs and expands slowly; infantry upgrade first."},
     {"Best Friend", "Fortifies its legendary planets and Mecatol Rex with ground forces and PDS."}},
    {"argent", {"Wingmen", "Proposes joint attacks and guards its partners' systems."},
     {"Strike Doctrine", "Loves destroyers and barrage; hunts fighter-heavy fleets."}},
    {"cabal", {"Hungry Void", "Keeps its fleet near its gravity rifts and lures enemies in."},
     {"Soul Collector", "Chases fights against big ships to capture them."}},
    {"empyrean", {"Void Wardens", "Holds empty space and frontier systems."},
     {"Dark Bargainers", "Pushes Dark Pact and Blood Pact deals."}},
    {"ghost", {"Wormhole Weavers", "Strikes through wormholes into enemy backlines."},
     {"Phantom Raiders", "Hit and run: attacks often, retreats early."}},
    {"hacan", {"Wheel and Deal", "Hoards trade goods and action cards (a card is worth 3); spends only on fleets and objectives."},
     {"Scarface", "Offers action cards (worth 2) and leans on players to buy."}},
    {"jolnar", {"Ivory Tower", "Research first; avoids fights."},
     {"Arms Dealers", "Trades its Research Agreement and makes technology-for-goods deals."}},
    {"l1z1x", {"Maximum Impact", "Dreadnought fleets for invasions and threats; dreadnought upgrade first."},
     {"Assimilation Protocol", "Targets planets with enemy PDS and space docks to take them over intact."}},
    {"letnev", {"Iron Barons", "Dreadnought-heavy fleets."}, {"War Bankers", "Hoards trade goods and lends them to neighbours."}},
    {"mahact", {"Subjugation", "Picks small fights to collect command tokens."},
     {"Annihilation", "Big fleets, big fights; demands trade goods and notes."}},
    {"mentak", {"Cheapskates", "Values promissory notes 1 trade good lower and action cards 1 higher."},
     {"Corsairs", "Raids lone ships and pillages whenever it can."}},
    {"muaat", {"Terrorist", "Bullies and threatens often; its hero hits a valuable system of whoever refuses."},
     {"New Emperor", "War suns, the war sun upgrade and its flagship first; threatens invasion."}},
    {"naalu", {"Crystal Swarm", "Favours fighters and the fighter upgrade."},
     {"Slithering Tongue", "Pays above value for promissory notes and offers a little extra."}},
    {"naaz", {"Scrap Divers", "Explores everything and chases relics."},
     {"Odd Couple", "Keeps mechs with its fleet for Z-Grav fights."}},
    {"nekro", {"Tech Parasite", "Picks fights with its most advanced neighbour to steal technology."},
     {"Plague Spreader", "Spreads small forces into many systems."}},
    {"nomad", {"Drifters", "Its fleet never stops moving; home is lightly guarded."},
     {"Mercenaries", "Values trade goods highly and sells its notes readily."}},
    {"saar", {"Wanderers", "Moves with its fleet; home is rarely defended."},
     {"Junk Barons", "Grabs many cheap planets and swaps commodities eagerly."}},
    {"sardakk", {"Blood Rage", "Never retreats; attacks its nearest neighbour in rounds 2-3."},
     {"Hive Command", "Masses ground forces for a late push on a home system."}},
    {"sol", {"Imperium", "Spreads infantry wide; values Support for the Throne and ceasefires."},
     {"Helldivers", "Drops big infantry stacks and invades at narrower odds."}},
    {"titans", {"Sleeping Giant", "Quiet until round 4, then wakes up."}, {"Iron Shepherd", "Fortifies its planets with PDS."}},
    {"winnu", {"Heirs of Lazax", "Obsessed with taking and holding Mecatol Rex."},
     {"Old Diplomats", "Hands out Acquiescence easily and sells its votes."}},
    {"xxcha", {"Diplomatic Corps", "Seeks peace and votes to stop wars."}, {"Shell Defense", "PDS everywhere; rarely leaves its borders."}},
    {"yin", {"Grey Fanatics", "Harasses with small fights; arms a betrayal at the start of round 2."},
     {"Martyrs", "Throws cruisers and destroyers into fights freely."}},
    {"yssaril", {"Card Sharks", "Hoards and plays many action cards."},
     {"Ghost Fleet", "Its flagship roams behind enemy lines hunting lone ships."}},
    // 0.21.0 (owner: "two new faction trait modifiers" for the Keleres)
    {"keleres", {"Internal Affairs", "The galaxy's secret police: goes after players who break promises or attack others, and offers protection to their victims."},
     {"Gavel of the Council", "Plays the council: blanks laws whenever one is in its way, votes with everything it has and keeps ships around Mecatol Rex."}},
    // 0.35.0: the Thunder's Edge factions (from the faction guides; the Firmament's stay with the Obsidian)
    {"bastion", {"Phoenix Banner", "Looks for small fights it is sure to win, to earn galvanize tokens, and keeps its galvanized units at the front."},
     {"Liberator", "Lands plenty of infantry so Liberate readies its new planets, and offers protection to neighbours under attack."}},
    {"ralnel", {"Last Gecko Out", "Keeps a cheap action in hand so it is the last to pass each round (Director Nel, Data Skimmer)."},
     {"Skittering Ring", "Never piles everything into one system: small groups of ships around its planets, ready for Survival Instinct."}},
    {"crimson", {"Empty House", "Leaves its home unguarded and throws the whole fleet forward, with Exile destroyers beside the target."},
     {"Quiet Breaches", "Opens breaches inactive, makes one active only when it uses it, and closes its own at the status phase - but keeps the Sorrow's, the one its flagship holds and one where its mech can take a planet."}},
    {"deepwrought", {"Visiting Scholar", "Coexists whenever ground forces meet its own, unless it is sure to win the fight."},
     {"Library Card", "Fires Visionaria Select as soon as two rivals hold 3 trade goods, and climbs one colour with Radical Advancement every round."}},
    {"firmament", {"Puppet Master", "Courts every neighbour early and hands out Black Ops, choosing each plot to suit its victim."},
     {"Coup Clock", "Banks 3 trade goods on The Sowing every status phase and becomes the Obsidian the moment it holds 2 plots - round 3 at the latest."}},
    {"obsidian", {"Puppet Master", "Courts every neighbour early and hands out Black Ops, choosing each plot to suit its victim."},
     {"Coup Clock", "Banks 3 trade goods on The Sowing every status phase and becomes the Obsidian the moment it holds 2 plots - round 3 at the latest."}},
    // 0.37.0: Discordant Stars batch 1
    {"kortali", {"Exhausted Prey", "Waits for rivals to spend their planets, then strikes them (Ruthless), and keeps its dreadnoughts on the border."},
     {"Zealot's Banner", "Never takes Warfare itself: it follows whoever does with Zealous and keeps a strategy token for it."}},
    {"vaylerian", {"Corsair Code", "Hunts small fleets that can't get away and fills its docks with raider cruisers."},
     {"Card Sharp", "Hoards action cards, takes Politics early and spends cheap cards to ready every planet it takes (Scour)."}},
    {"lanefir", {"Grave Robber", "Explores every planet and frontier token in reach and fights for the chance to explore again."},
     {"Iconoclast Scholar", "Spends its relic fragments on research and mechs instead of relics."}},
    {"kyro", {"Patient Plague", "Takes Politics whenever it can, for Contagion's free infantry."},
     {"Pale Rider", "Votes with its infantry (Silas Deriga) and rides every agenda it is sure of."}},
    {"cheiran", {"Hive Bastion", "Builds structures and mechs on everything it holds and never leaves its home alone."},
     {"Moulting Season", "Lets enemies come to it: fights best as the defender among its structures, with dreadnoughts in front."}},
    {"celdauri", {"Merchant Princes", "Trades with everyone, washes commodities and sells its Trade Alliance."},
     {"Dock Barons", "A space dock on every good planet - George Nobin every turn it can, Construction for docks."}},
    // 0.38.0: Discordant Stars batch 2
    {"khrask", {"Stone Garden", "Keeps its planets clear of ground forces for Garden Worlds and guards them with PDS instead."},
     {"Crystal Bloc", "Votes as a block of stone: takes Politics and buys votes with trade goods."}},
    {"zealots", {"Relic Hunters", "Goes out of its way for planets with technology specialties and researches at every chance."},
     {"Last Word", "Always takes the last vote with Conspirators and leans on Politics."}},
    {"zelian", {"Purifier's Zeal", "Hunts weak planets to bombard, and loves a fight it is sure to win."},
     {"Hammerfall", "Builds Impactor infantry in numbers and carries them everywhere."}},
    {"rohdhna", {"Foundry Lords", "Turns every new space dock it can into a war sun."},
     {"Scrap Economy", "Keeps trade goods in hand and trades its old hulls for more."}},
    {"tnelis", {"Shadow Brokers", "Chases secret objectives: Imperial first, every draw it can get."},
     {"Runner Fleets", "Swarms of Blockade Runner destroyers raiding where others can't follow."}},
    {"kolume", {"Silent Gunners", "Moves its gun-ships together and leads with dreadnoughts."},
     {"Patient Order", "Takes Leadership for the tokens it can't rearrange and researches steadily."}},
    {"bentor", {"Map Collectors", "Explores everything twice: builds docks on its planets and spreads out wide."},
     {"Company Store", "Trades its commodities freely and lends a hand with Mgur's dividends."}},
    {"dihmohn", {"Grand Armada", "Fields destroyers by the dozen and keeps its dreadnoughts in the line."},
     {"Pressgangs", "Explores to build: every new planet is a shipyard (Impressment Programs)."}},
    {"florzen", {"Swarm Lords", "Lives on fighters: more Corsairs, more carriers, sold into every fight."},
     {"Extortionists", "Keeps a token for Blackmail and saves it for the leader's best strategy card."}},
    {"gledge", {"Deep Core Miners", "Mechs on every planet and Mantle Cracking as early as it can."},
     {"Platform Builders", "Builds PDS for the Orion Platforms and takes Construction."}},
    {"augers", {"Foresight", "Plans for the objective it knows is coming next and takes Imperial."},
     {"Data Brokers", "Keeps trade goods for the Sentient Datapool and researches at every status phase."}},
    {"kjalengard", {"Saga Seekers", "Picks fights it can win to plant Glory tokens across the board."},
     {"Shield Wall", "Carriers full of fighters and infantry - the fallen come back as command tokens."}},
    {"kollecc", {"Hidden Hand", "Keeps a cloaked reserve and strikes where nobody expects a fleet."},
     {"Relic Collectors", "Chases relic fragments - drones, digs and Captain Dust every round."}},
    {"mortheus", {"Mirror Wall", "Sits tight and lets attackers break on copies of their own ships."},
     {"Void Foundries", "Builds in empty space and spreads its fleets through it."}},
    {"vaden", {"Loan Sharks", "Takes the strategy cards others want - every secondary is a debt."},
     {"Gold Engines", "Keeps trade goods on hand for faster ships and extra hits."}},
    {"cymiae", {"Card Hoarders", "Lives on action cards, the discard pile its second deck."},
     {"Undying Horde", "Infantry everywhere - they always come back."}},
    {"nivyn", {"Rift Riders", "Moves through gravity rifts and anomalies others avoid."},
     {"Wound Keepers", "Holds its ground where the Wound makes every attacker pay."}},
    {"olradin", {"Popular Front", "Spreads out over planets of every trait to use its policies."},
     {"Council Hall", "Takes Politics and votes in blocs."}},
    {"freesystems", {"Liberators", "Rallies ships into the free worlds and lands Liberator mechs beside them."},
     {"Diplomatic Corps", "Spends the free worlds' planets through its Diplomats and votes with all three traits."}},
    {"edyn", {"Sigil Wardens", "Mechs on anomalies: Sigils that close the lanes and catch command tokens."},
     {"Oracles", "Predicts every agenda and leans on Politics and Imperial."}},
    {"nokar", {"Privateers", "Destroyers everywhere - and hires them out to anyone who pays."},
     {"Mercenary Admiralty", "Keeps a great destroyer fleet around the Annah Regia."}},
    {"axis", {"War Profiteers", "Sells Axis Orders to every neighbour for trade goods."},
     {"Shipyards", "Docks and dreadnoughts, with Industry Secrets in its customers' hands."}},
    {"mykomentori", {"Fungal Bloom", "Turns its infantry into Mycelium Rings wherever it lands."},
     {"Seers", "Saves its Omen dice for the rolls that matter most."}},
    {"ghemina", {"Twin Raiders", "The Lady and the Lord raid side by side - the Rule of Two."},
     {"Plunderers", "Strikes planets with structures for the Lady's trade goods."}},
    {"veldyr", {"Board of Directors", "Plants Branch Offices on every neighbour and lives on the dividends."},
     {"Lancer Line", "Leads with Lancer dreadnoughts - bombardment and space cannon in one hull."}},
    {"lizho", {"Trapmasters", "Seeds every planet it holds with traps."},
     {"Bomber Wings", "Heavy Bombers by the dozen."}},
    {"mirveda", {"Flying Batteries", "Gauss Cannons travel with every fleet."},
     {"Drone Carriers", "Carriers full of fighters that land as infantry."}},
    {"ghoti", {"Wandering Court", "Keeps the All Mother moving and produces wherever she goes."},
     {"Deep Void", "Spreads into empty systems for commodities."}},
};
}  // namespace

NamedText factionQuirk(int faction, int which) {
    if (faction < 0 || faction >= kFactionCount || which < 0 || which > 1) return {"", ""};
    for (const FQ& f : kFactionQuirks)
        if (!std::strcmp(f.faction, kFactions[faction].key)) return which ? f.b : f.a;
    return {"", ""};
}

bool fquirkIs(const Game& g, int p, const char* key, int which) {
    return isBot(g, p) && g.players[p].pers.fquirk == which && !std::strcmp(kFactions[g.players[p].faction].key, key);
}

uint32_t prand(const Game& g, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t mod) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t(g.seed) * 0xBF58476D1CE4E5B9ull);
    for (uint32_t v : {a, b, c, d}) {
        h ^= uint64_t(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
    }
    return mod ? uint32_t(h % mod) : uint32_t(h);
}

std::string archName(const Game& g, int p) {
    if (!isBot(g, p)) return "";
    return kArchetypes[g.players[p].pers.arch].name;
}

float attitudeScore(const Game& g, int bot, int q) {
    if (!isBot(g, bot) || q < 0 || q >= g.numPlayers || q == bot) return 15.f;
    const Personality& pe = g.players[bot].pers;
    // relative to the archetype's starting grudge, so a Betrayer doesn't look hostile from the start
    static const float kMood[int(Arch::Count)] = {0, -5, 8, -6, 0, -2, 2, -3, 3, 4};  // how it shows itself
    const int a = pe.now >= 0 ? pe.now : pe.arch;
    float s = 15.f + (pe.bf[q] - kArchetypes[pe.arch].bStart) + kMood[a];
    s += 3.f * pe.attacks[q] + 4.f * pe.broke[q] - 2.f * pe.deals[q] - 2.f * pe.kept[q] - 0.5f * pe.gifts[q];
    if (hostileFlag(g, bot, q)) s += 12.f;
    Relation r = relationOf(g, bot, q);
    if (r == Relation::Contract) s -= 6.f;
    else if (r == Relation::Promise) s -= 3.f;
    return s;
}

Attitude attitudeOf(const Game& g, int bot, int q) {
    if (!isBot(g, bot) || q < 0 || q >= g.numPlayers || q == bot) return Attitude::Wary;
    const float s = attitudeScore(g, bot, q);
    if (s < 11.f) return Attitude::Friendly;
    if (s >= 33.f) return Attitude::Hostile;
    return Attitude::Wary;
}

const char* attitudeName(Attitude a) {
    switch (a) {
        case Attitude::Friendly: return "Friendly";
        case Attitude::Hostile: return "Hostile";
        default: return "Wary";
    }
}

}  // namespace ti
