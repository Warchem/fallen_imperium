// The rules engine asks one player one question at a time (a Decision) and
// waits for an Answer. Humans answer through the UI, AI players through the
// ai module. Every answer is recorded, which is also how games are saved.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ti {

enum class DecisionKind : uint8_t {
    Pick,     // choose option(s) from a list (or systems / planets on the map)
    Pay,      // pick planets to exhaust (+ trade goods) to pay a cost
    Move,     // tactical action movement plan
    Produce,  // what to build
    Hits,     // assign hits to your units (or choose units to remove)
    Land,     // assign units to planets (landing ground forces, or bombardment targets)
    Tokens,   // distribute command tokens between pools
    Deal,     // compose a deal offer (Negotiations): Answer::data = encodeDeal(), empty = cancel
};

// Why the question is being asked (lets the AI and UI tailor themselves).
enum class Tag : uint8_t {
    Generic,
    SetupTech,        // choose a starting technology
    SetupSecret,      // keep 1 of 2 secret objectives
    StrategyPick,     // strategy phase card choice
    ActionChoice,     // tactical / strategic / pass
    Activate,         // system to activate
    Movement,
    SpaceCannonFire,  // fire space cannon?  (yes/no)
    AssignHits,
    Retreat,          // announce a retreat? (yes/no)
    RetreatTo,        // system to retreat to
    Bombard,          // pick the planet to bombard
    LandForces,
    Custodians,       // pay 6 influence to remove the custodians token?
    Production,
    Payment,
    Research,         // pick a technology
    SecondaryUse,     // use a strategy card's secondary ability? (yes/no)
    Leadership,       // influence to spend on command tokens
    DiplomacySystem,
    ReadyPlanets,
    PoliticsSpeaker,
    ConstructionWhat,
    ConstructionWhere,
    TradeReplenish,
    WarfareToken,     // command token to take back
    ConstructionSystem,
    WarfareDock,
    ScorePublic,
    ScoreSecret,
    ImperialScore,
    TokenGain,        // place newly gained tokens
    TokenRedistribute,
    SkipPrereq,       // exhaust a tech-specialty planet to skip a prerequisite
    UseTech,          // optional technology use (yes/no)
    DiscardSecret,
    ExtraResearch,    // pay 6 resources for a second technology? (yes/no)
    ActivateConfirm,  // nothing can move into / produce in this system: activate anyway?
    TokenSpend,       // how many tokens to spend from tactic vs strategy pool
    RemoveExcess,     // choose units to remove (fleet pool / capacity limits)
    CannonTarget,     // which player's ships to fire space cannon at
    TokenFromPool,    // no tokens in reinforcements: which pool to take one from
    BombardPlan,      // which planet each bombarding unit fires at (Land kind)
    // Stage 3: action cards, agendas, promissory notes
    PlayActionCard,   // a timing window: play one of these cards? (options = card ids, -1 = no)
    CardTarget,       // a choice while resolving an action card (d.card says which)
    DiscardActionCard,// over the hand limit: discard one
    VoteOutcome,      // agenda: which outcome to vote for (-100 = abstain)
    VotePlanets,      // agenda: which planets to exhaust for votes (Pay kind)
    Predict,          // rider: predict the outcome
    TieBreak,         // speaker decides a tied vote
    AgendaPeek,       // Politics: put each of the top 2 agendas on top or bottom
    AgendaChoice,     // other choices made while resolving an agenda (d.agenda says which)
    GiveNote,         // choose a promissory note to give away
    PlayNote,         // play a promissory note now? (options = notes, -1 = no)
    LawAbility,       // use a law you own now? (Arbiter, Committee, Minister of Peace / War)
    StrategyGive,     // Checks and Balances: who receives the strategy card
    ComponentChoice,  // a choice while resolving a technology's component action
    // Stage 4: exploration and relics
    ExploreChoice,    // a choice while resolving an exploration card (d.explore says which)
    RelicChoice,      // use a relic / a choice while resolving one (d.relic says which)
    LegendaryAbility, // use a legendary planet's ability (d.planet) / a choice while resolving it
    ExploreOrder,     // which planet / deck to explore
    // Stage 5: faction abilities
    Faction,          // use a faction ability / leader / faction note, or a choice while resolving one (d.ability says which)
    // Stage 6: negotiations
    Negotiate,        // negotiate with whom? (options = players, -1 = done)
    DealOffer,        // compose an offer to d.targetPlayer (Deal kind)
    DealResponse,     // answer d.deal: 1 accept, 0 decline, 2 counter
    DealCounter,      // compose a counter-offer to d.targetPlayer (Deal kind; d.deal = the offer being countered)
    // 0.14.0: Trade card X-1 trades
    TradeX1Offer,     // Trade primary: which players to offer an X-1 trade (pick many)
    TradeX1Accept,    // offered an X-1 trade by d.targetPlayer: 1 accept, 0 decline
    GalaxyPlace,      // galaxy setup draft (0.15.0): place a tile from your hand; option id = tile * 1000 + draft cell
    LoanRepay,        // pay back a loan now? (yes / no)
    // 0.17.0: the community X-1 (Setup::trade bit 3)
    TradeX1Settle,    // refreshed for an X-1 trade with d.targetPlayer: 1 send the commodities for X-1 trade goods, 0 keep them
    // 0.34.0: Thunder's Edge's revised Construction - 1 place a structure, 2 produce at one of your space docks
    ConstructionChoice,
    ConstructionDock,  // ... at which space dock (system * 64 + planet + 1; 0 = floating factory: system * 64)
    OceanPay,          // 0.35.0: how many ocean cards (Deepwrought) to exhaust toward a payment
    EngageCoexisting,  // 0.35.0: fight another player's coexisting units on the planet you invade (1) or leave them (0)
    RetreatCargo,      // 0.54.0 (fixes 18): how many ground forces of one kind (d.value = planet * 16 + type) the retreating ships take along
};

// Faction abilities, leaders, faction notes and unit abilities that ask questions
// (Decision::ability). Numbers are stable: they go into Decision only, not saves.
enum class Ab : int16_t {
    None = 0,
    // Federation of Sol
    OrbitalDrop, SolMechDeploy, MilitarySupport, SolAgent, SolCommander, SolHero,
    // Barony of Letnev
    Munitions, LetnevMechDeploy, WarFunding, LetnevAgent, LetnevHero,
    // Emirates of Hacan
    QuantumDatahub, ProductionBiomes, WrathOfKenara, HacanAgent, HacanCommander, HacanHero,
    // Universities of Jol-Nar
    Brilliant, SpatialConduit, ResearchAgreement, JolnarAgent, JolnarCommander, JolnarHero,
    // Sardakk N'orr
    Exotrireme, Tekklar, SardakkAgent, SardakkCommander, SardakkHero,
    // Xxcha Kingdom
    PeaceAccords, Quash, InstinctTraining, NullificationField, PoliticalFavor, XxchaAgent, XxchaHero,
    // batch 2: Arborec
    Mitosis, Bioplasmosis, ArborecFlagship, Stymie, ArborecAgent, ArborecCommander, ArborecHero,
    // L1Z1X Mindnet
    Harrow, InheritanceSystems, L1Z1XAgent, CyberneticEnhancements, L1Z1XHero,
    // Mentak Coalition
    Ambush, Pillage, SalvageOps, MentakAgent, MentakCommander, MentakHero, PromiseOfProtection,
    // Naalu Collective
    Foresight, GiftOfPrescience, NaaluAgent, NaaluHero,
    // Yin Brotherhood
    Indoctrination, Devotion, YinSpinner, ImpulseCore, Greyfire, YinAgent, YinHero,
    // Yssaril Tribes
    StallTactics, YssarilMechDeploy, Scheming, MageonImplants, SpyNet, YssarilCommander, YssarilHero,
    // general
    InfantryReturn,
    // batch 3: Creuss
    WormholeGenerator, CreussIff, IcarusDrive, CreussAgent, CreussCommander, CreussHero, DimensionalSplicer,
    // Muaat
    StarForge, EmberColossus, MuaatAgent, MuaatHero, FiresOfGashlai, Inferno,
    // Saar
    ScavengerZeta, SaarAgent, SaarCommander, SaarHero, RaghsCall, ChaosMapping,
    // Winnu
    Reclamation, WinnuAgent, WinnuHero, Acquiescence, LazaxGate, HegemonicTrade, Reclaimer, BloodTies,
    // Nekro
    GalacticThreat, TechSingularity, NekroAgent, NekroHero, Antivirus, Alastor,
    // Empyrean
    Aetherpassage, EmpyreanAgent, EmpyreanCommander, EmpyreanHero, BloodPact, Aetherstream, Dynamo, Watcher,
    // batch 4: Argent Flight
    RaidFormation, ArgentAgent, ArgentHero, Ambuscade,
    // Mahact Gene-Sorcerers
    MahactAgent, MahactHero, Starlancer, CrimsonLegionnaire, GeneticRecombination, Scepter,
    // Naaz-Rokha Alliance
    DistantSuns, Fabrication, NaazAgent, NaazCommander, NaazHero, BlackMarketForgery, Supercharge,
    // Nomad
    NomadMercer, NomadThundarian, NomadArtuno, NomadHero, QuantumManipulator, Cavalry, TemporalCommand,
    // Titans of Ul
    Terragenesis, Awaken, Ouranos, Hecatoncheires, TitansAgent, TitansHero, Terraform,
    // Vuil'raith Cabal
    Amalgamation, Riftmeld, CabalAgent, CabalHero, Crucible, Vortex,
    // batch 5 (0.21.0): the Council Keleres
    LawsOrder, KeleresAgent, KeleresCommander, KeleresHarka, KeleresOdlynn, KeleresKuuasi, KeleresRider, ExecutiveOrder,
    AgencySupply,
    // 0.30.0 (new games, Setup::extras bit 4)
    AiDevAlgorithm,
    // 0.32.0 (new games, Setup::extras bit 5): Trade Convoys is played as an action
    TradeConvoys,
    // 0.32.1 (new games, Setup::extras bit 6): Dark Pact is played as an action
    DarkPact,
    // Thunder's Edge (0.34.0): the expedition, Thunder's Edge, the Fracture
    Expedition, ExpeditionPay, TePlace, TeInfantry, IngressPlace, FractureRoll,
    // ... breakthroughs
    BtArborec, BtLetnev, BtSardakk, BtSol, BtWinnu, BtCreuss, BtHacan, BtMahact, BtXxcha, BtNomad, BtJolnar, BtNaalu,
    BtSaar, BtCabal, BtMentak,
    BtMuaat, BtYin, BtTitans, BtNaaz, BtEmpyrean, BtArgent, BtNekro, BtKeleres, BtYssaril,
    // Thunder's Edge factions (0.35.0)
    Galvanize, PhoenixStandard, RaiseStandard, BastionAgent, BastionHero, Proxima, BtBastion, Hyperion,
    SurvivalInstinct, Miniaturization, NanoLink, LastDispatch, RalNelCommander, Alarum, RalNelHero, BtRalNel, RalNelAgent,
    Nano1, Nano2, Nano3,
    ResearchTeam, FirmamentMech, DeepwroughtAgent, DeepwroughtCommander, Radical, Eanautic, DeepwroughtHero, ShareKnowledge,
    BtDeepwrought,
    Incursion, Exile, RemoveBreach, Subatomic, CrimsonHero, Revenant, CrimsonAgent, Sever, BtCrimson,
    // the Firmament / the Obsidian
    PlotsWithin, PlotCard, Puppets, FirmamentAgent, FirmamentHero, PlotToken, BlackOps, NeuralParasite, Sowing, Seethe,
    Extract, Enervate, ObsidianAgent, ObsidianHero, Malevolency, Planesplitter, Parasite2,
    // galactic events (0.36.0)
    EvMobilize, EvAtomics, EvTotalWar, EvMercenary, EvExplore, EvX89, EvSpoils, EvWildsTech, EvMonument, EvWormhole,
    // Discordant Stars batch 1 (0.37.0): Kortali, Vaylerian, Lanefir, Kyro, Cheiran, Celdauri
    DsZealous, DsRuthless, DsMagistrate, DsTempest, DsDeliverance, DsBlessing, DsLucreia, DsLorena, DsNadalia,
    DsCargoRaiders, DsCargoPay, DsScour, DsRaze, DsLostCause, DsScavenger, DsClansFavor, DsYvin, DsPyndil, DsDyln, DsEclipse,
    DsEdifice, DsIconoclasm, DsWarStories, DsMemoryDusk, DsSpark, DsSparkMove, DsAts, DsSpoils, DsVassa, DsHalbert,
    DsVenerable, DsTroubadour,
    DsContagion, DsAuriga, DsIndoctrination, DsVector, DsKyroRider, DsTox, DsSilas, DsSpeygh, DsPustule,
    DsMoult, DsChitin, DsBroodPod, DsCarcinisation, DsKkavras, DsPhquaiset, DsThakt, DsNauplius,
    DsProjection, DsEmergencyMob, DsNobin, DsTitus, DsMinuteman, DsSupremacy,
    // Discordant Stars batch 2 (0.38.0): Khrask, Rhodun, Zelian, Roh'Dhna, Tnelis, Kolume
    DsHyperkinetic, DsMeteorSlings, DsStoneSpeakers, DsBypass, DsZelianAgent, DsCollider, DsDavish, DsDaedalonTech, DsObsessive,
    DsPriestess, DsPilgrimage, DsTnelisNote, DsTurra, DsDeliberate, DsStoneEmbrace, DsIndustrious, DsCataclysm, DsDaedalon,
    DsPrimespace, DsRondBriay, DsAutomatons, DsContractual, DsVehlTikar, DsSaintBinal, DsMeditation, DsBiothermics, DsFran,
    DsWonell, DsSanctification, DsOmniscience, DsRecycled, DsAutofabricator, DsKyvir, DsConspirators, DsBUnit, DsSaintKeep,
    DsFillipo, DsUdosh,
    // Discordant Stars batch 3 (0.39.0): Bentor, Dih-Mohn, Florzen, Gledge, Augurs, Kjalengard
    DsSecretMaps, DsAuctioneer, DsReplicators, DsEncryptionKey, DsMgur, DsKenTucc,
    DsMigrant, DsMaximus, DsAegis, DsImpressment, DsImpressAction, DsCombatDrills, DsJginFaru, DsYpru,
    DsMercenaries, DsMercTarget, DsDataLeak, DsBlackMarkets, DsBlackmail, DsUnderground, DsSalGavda, DsQuaxdol, DsBanua,
    DsPrivateer,
    DsMantle, DsDeepMining, DsGledgeBase, DsDurran, DsGorthrim,
    DsPsychographics, DsDatapool, DsClodho, DsAtropha, DsIledrith,
    DsGlory, DsHeroism, DsHulgade, DsZhrgar, DsVassalage, DsMerkismathr, DsYgegnad,
    // Discordant Stars batch 4 (0.40.0): Kollecc, Mortheus, Vaden, Cymiae, Nivyn, Olradin
    DsCloaked, DsShroud, DsTreasure, DsNightingale, DsSeeker, DsSkirmishers, DsAiSurvey, DsCaptainDust, DsGriphyn, DsNightshade,
    DsFacsimile, DsIllusory, DsSieve, DsFractal, DsWalik, DsBayan, DsDuuban,
    DsFinePrint, DsCollateral, DsBindingDebts, DsMidas, DsKrovoz, DsKrovozHit, DsYudri, DsPutriv, DsPutrivPay, DsCollector,
    DsAutonetic, DsSkhot, DsRecursiveWorm, DsKoryl, DsVoiceUnited, DsAlgorithmic,
    DsVoidsailors, DsVoidwake, DsWarden, DsGuidance, DsSuldhan, DsKwill,
    DsPolicy, DsConnect, DsPreserve, DsExploit, DsPlunder, DsFalseFlag, DsGeosympathic, DsIncite, DsBaggil, DsKnak, DsPahn,
    // Discordant Stars batch 5 (0.41.0)
    DsRally, DsDiplomats, DsCovert, DsBroadcast, DsCordo, DsCyhn, DsOtto, DsLiberator,                         // Free Systems
    DsGrace, DsRadiance, DsUnity, DsEdynRider, DsAllant, DsAllantPeek, DsKadryn, DsMidir, DsSigilToken,         // Edyn
    DsHired, DsHiredBuy, DsNavigator, DsSabre, DsLocalContracts, DsSalSparrow, DsJackHallard, DsStarsails,     // Nokar
    DsOrderBuy, DsOrderUse, DsRiftEngines, DsEmergency, DsIndustrySecrets, DsShipmonger, DsTckVsk, DsDemiQueen, // Axis
    DsBearer, DsOmen, DsLactarius, DsNecrophage, DsPsychoactive, DsInsight, DsInsightReroll, DsAmanita,          // Myko-Mentori
    DsSoulCage, DsAmandia, DsMycelium,
    DsWhichFlagship, DsWarSong, DsRaidLeaders, DsSkarvald, DsKorela, DsKantrus, DsTransport, DsJarl,           // Ghemina
    // Discordant Stars batch 6 (0.42.0)
    DsBranchPlanet, DsHolding, DsTargeted, DsSeidr, DsSolis, DsAuberon, DsAurora,                               // Veldyr
    DsTrapPlace, DsTrapPlanet, DsTrapReveal, DsGridTarget, DsSiphonTake, DsWraith, DsCounselor, DsVasra,          // Li-Zho
    DsKhazRin, DsOroZhin,
    DsCombatDrones, DsOrbitalGrid, DsOrbitalShield, DsExcavation, DsLogicMachina, DsAssaultMachina, DsWrathMachina, // Mirveda
    DsParallel, DsGhotiRelay, DsBecece, DsCeie, DsNmenmede, DsTioleombp, DsNetworked, DsGhotiPlanet,             // Ghoti
    // 0.43.0 (the owner's RULES-CHECK answers)
    DsEnvoy,  // Envoy Network: exhaust a cultural, a hazardous and an industrial planet at the start of the agenda phase
    DsHireOffer, DsHirePick,  // Hired Guns as a deal: offer ships to the active player, then pick the ones sold
    DsPlague,                 // Plague Reservoir: a ground combat with nobody there
    // Discordant Stars breakthroughs (0.43.0, rules_dsbt.cpp)
    DsbtAxis, DsbtCeldauri, DsbtCymiae, DsbtGhoti, DsbtAugers, DsbtKolume, DsbtFlorzen, DsbtLanefir, DsbtNokar, DsbtZealots,
    DsbtBentor, DsbtEdyn, DsbtFreesys, DsbtLizho, DsbtTnelis, DsbtOlradin, DsbtKhrask, DsbtMyko, DsbtVaylerian, DsbtMortheus,
    DsbtKollecc, DsbtDihmohn, DsbtGhemina, DsbtCheiran, DsbtVaden, DsbtKyro,
    ScarTech,  // 0.43.4: an entropic scar at the start of the status phase - a strategy token for a faction technology
    SardakkCommanderUnit,  // 0.44.2: G'hom Sek'kus - infantry or mech from a planet with both (Decision::value = planet)
    DsCelagrom,  // 0.52.1: the Celagrom's hit at the start of a space combat (option = the ship type, -1 = none)
    CrimsonCommander,  // 0.58.2: Ahk Siever after a combat - 1 gain a commodity, 2 turn one into a trade good, 0 neither
    MoveBreach,        // 0.59.0: all 7 breach tokens are out - the inactive breach to move (option = its system)
    YinCommander,      // (fixes 26, the Yin review) Brother Omar Omega: which infantry goes back (asked of a person)
    PrideOfKenara,     // (fixes 26, the Hacan review) the planet a traded Pride of Kenara planet's units move to (option = planet)
    NaaluMechTE,       // (fixes 27, the Naalu review) Iconoclast (Thunder's Edge): a mech when another player gains a relic
    SaarCommanderCount,  // (fixes 27, the Saar review) Rowl Sarrig: how many of the new fighters / infantry go to that dock
    Count
};

struct Option {
    int id = 0;
    std::string label;
    std::string detail;
    bool enabled = true;
};

// 0.43.0: in Decision::context, the text after this character is a rule reminder the screens draw in red
constexpr char kRedNote = '\x01';

struct Decision {
    int player = -1;
    DecisionKind kind = DecisionKind::Pick;
    Tag tag = Tag::Generic;
    std::string prompt;   // short question
    std::string context;  // extra explanation

    // Pick
    std::vector<Option> options;
    int minPick = 1, maxPick = 1;
    bool onMap = false;      // options are systems (id = system index) to choose on the map
    bool onPlanets = false;  // options are planets (id = planet index)

    // Pay
    int amount = 0;          // resources or influence needed
    bool influence = false;  // pay influence instead of resources
    bool mixed = false;      // each planet pays its resources or its influence (Freelancers)
    int amount2 = 0;         // second amount (influence) for "spend both" objectives
    bool allowTradeGoods = true;
    std::vector<int> planets;  // Pay: planets that may be exhausted; Land: target planets (empty = all)

    // Hits / Land / Move / Produce context
    int system = -1;
    int planet = -1;  // -1 = space area
    int hits = 0;
    int hitMask = 0;  // which unit types may take these hits (bit per UnitType), 0 = any
                      // Land: which unit types can be assigned (0 = ground forces)
    int targetPlayer = -1;
    bool noSustain = false;  // Hits: removing units, Sustain Damage can't be used
    int window = -1;         // PlayActionCard: which timing window (Rules::Win)
    int card = -1;           // action card being resolved
    int agenda = -1;         // agenda being voted on / resolved
    int value = 0;           // extra number (hits to cancel, votes so far, ...)
    int costCap = 0;         // Produce: the combined cost may not be higher (Maximus, Discordant Stars); 0 = no cap
    bool mustProduce = false;  // Produce: at least 1 unit if possible (Contractual Obligations, 0.44.5)
    int predicted = -1;        // VoteOutcome: the outcome this voter predicted (Radiance / Unity Algorithm, 0.44.6), -1 none
    int explore = -1;        // exploration card being resolved
    int relic = -1;          // relic being used
    int ability = 0;         // Ab: faction ability / leader / note being used

    // Tokens
    int newTokens = 0;
    bool redistribute = false;

    // Deal: the deal being answered / countered (encodeDeal), and what may be part of it
    std::vector<int> deal;
    int dealFlags = 0;  // kDealComponents, kDealActionCards, kDealVote ... (deal.h)

    std::string error;  // why the previous answer was rejected

    // Set by the rules: returns "" if an answer is acceptable.
    std::function<std::string(const struct Answer&)> validate;
};

struct Answer {
    std::vector<int> picks;  // option ids, planet indices, ...
    int number = 0;          // trade goods spent, counts, ...
    std::vector<int> data;   // plans (see plan encodings in rules.h)

    std::string encode() const;
    static bool decode(const std::string& s, Answer& out);
};

}  // namespace ti
