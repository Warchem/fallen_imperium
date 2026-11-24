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
    // 0.61.1 (fixes 32, LRR 23.4a): none of a unit type left in the reinforcements - which of yours comes back off the board to be
    // placed (option = reclaimId(system, planet, damaged), -1 = don't; d.value = destination system * 64 + unit type)
    ReclaimUnit,
    // (0.81.0) Absol's Agendas: Decision::agenda = the card, Decision::value = what is asked (AbsolAsk below)
    AbsolTie,            // the speaker breaks a tie for "the player who cast the most votes" (option = player)
    AbsolNote,           // which of your promissory notes to give (option = note; targetPlayer = who gets it)
    AbsolGiveTo,         // which player gets your promissory note (option = player)
    AbsolChoice,         // any other choice an Absol agenda asks for
    // (0.82.0) Monuments+
    MonumentPlace,       // where to place your monument (option ids: 100000 + planet, 200000 + system; -1 = not now)
    // a monument card's ability (Decision::ability = the kMonuments index of the card whose text is used, Decision::value = what is
    // asked - MonAsk in rules.h)
    MonumentAbility,
};

// (0.82.0) Tag::MonumentAbility: what is asked (Decision::value). Option ids as noted (-1 = don't / done).
enum MonAsk : int {
    kMonAskArborec = 1,   // Flaah Orbitals: the planet for the infantry (option = planet)
    kMonAskArgent,        // Argent action: the system (option = system)
    kMonAskCreuss,        // Revenance Circuit: systems with wormholes to hit (pick many; option = system)
    kMonAskJolnar,        // Calm Seas Sanatorium: the planet for up to 2 infantry (option = planet * 4 + count)
    kMonAskL1z1x,         // L1Z1X: a command token to move (option = system * 16 + owner)
    kMonAskMahact,        // Spire of Ixth: a command token into the fleet pool (option = owner)
    kMonAskMentak,        // Erwan's Fist: the system whose ships with SUSTAIN DAMAGE become damaged (option = system)
    kMonAskMuaat,         // Glory Furnace: 3 votes (0) or a hit (option = system * 16 + player)
    kMonAskNorr,          // G'hom Firefly Beacon: produce (option = planet: infantry; 50000 + system: fighter)
    kMonAskNorrReplace,   // ... at the end of your turn: replace it (option = unit type: PDS / space dock)
    kMonAskNomad,         // Lodestar: return it to produce the flagship here (1 yes / 0 no)
    kMonAskSaar,          // Beacon of Ragh: the space dock (system) it is treated as adjacent to
    kMonAskSol,           // The Cenotaph: how many of the infantry go to its planet instead (option = count)
    kMonAskTitans,        // Scepter of Ul: the planet for the sleeper token
    kMonAskCabal,         // She Consumes Worlds: DEPLOY (1 yes / 0 no)
    kMonAskWinnuTake,     // Imperial Vault: trade goods taken off the card (option = count)
    kMonAskWinnuStore,    // ... trade goods put on it, 3 resources each (option = count)
    kMonAskXxcha,         // Qanoj Shield Array: exhaust it to place a structure (1 / 0)
    kMonAskYin,           // The Blessed Sanctum: where an infantry goes (option = planet)
    kMonAskKeleresLaw,    // Keleres: a command token from the reinforcements onto the card (1 / 0)
    kMonAskKeleresToken,  // ... the token placed from the card (1 / 0)
    kMonAskNekro,         // Valefar Assimilator "M": which token goes onto the card (0 "M", 1 "Z")
    kMonAskNaalu,         // Nefishh Projector: the committed infantry coexist instead (1 / 0)
    kMonAskHacan,         // Mowshir Freeport: where the units gained go (option = system)
    kMonAskSolCommit,     // The Cenotaph on a commit: how many go to its planet instead (option = count)
    // (0.82.1) Thunder's Edge's monuments
    kMonAskBastion,       // Seraph Data Center lost: whose ships take the 2 hits (option = player / kNeutral)
    kMonAskCrimson,       // Veils of Lost Creuss: whose ships take the hit (option = player / kNeutral; -1 none)
    kMonAskDeepwrought,   // The Pelagion removed: the player who resolves a space combat there (option = player)
    kMonAskDeepwroughtFoe,  // ... that player's opponent (option = player / kNeutral)
    kMonAskEpiphanySkip,  // Epiphany: whose control token comes off the card to skip a prerequisite (option = player; -1 don't)
    kMonAskEpiphanyPlot,  // Epiphany: a control token from the card onto a plot card (option = plot * 16 + player; -1 don't)
    kMonAskObsidian,      // Epiphany Hollow: their infantry replaced with yours (option = planet * 4 + count; -1 don't)
    kMonAskRalnel,        // Sor Tek Linkhub: the structure to move (option = (system * 16 + owner) * 16 + type; -1 don't)
    kMonAskRalnelTo,      // ... the system it goes to (option = system)
    // (0.82.2) Discordant Stars' monuments, batch 1
    kMonAskLizho,         // Nightfall Fortress: whose command token comes off (option = player; -1 gain 1 command token)
    kMonAskEdyn,          // Twilight Throne at a ground combat's start: a mech now? (1 yes, 0 no)
    kMonAskKortali,       // Queen's Rest: whose ships take the hit (option = player / kNeutral)
    // (0.82.3) Discordant Stars, batch 2
    kMonAskMyko,          // Gravelord's Keep: a free fighter after an omen die (1 yes, 0 no)
    kMonAskDihmohn,       // Flotilla Cyclotron: +1 move to the ships in its system (1 yes, 0 no)
    kMonAskVaden,         // Bounty Brokerage: take 1 of their commodities (1 yes, 0 no; Decision::targetPlayer = them)
    kMonAskFlorzen,       // Corsairs' Cove: how many of the fighters just produced go onto the card (option = count)
    kMonAskFlorzenOut,    // Corsairs' Cove at the start of the turn: how many fighters come off the card (option = count, -1 none)
    kMonAskMortheus,      // Mirrorforge: whose ships, from where (option = system * 16 + player)
    kMonAskMortheusShip,  // ... the owner's next ship to move (option = unit type; asked of that player)
    kMonAskAugurs,        // AI Conclave: explore the Conclave's planet instead (1 yes, 0 no; Decision::planet = the other planet)
    kMonAskTnelisLoss,    // Dawnstar HQ: destroy every unit on the planet (1 yes, 0 no)
    kMonAskTnelis,        // Dawnstar HQ at the start of the turn: the coexisting unit (option = (planet * 16 + player) * 16 + type, -1 none)
    kMonAskVaylerian,     // Aylor Raider Hoard: exhaust to draw 1 action card (1 yes, 0 no)
    kMonAskFsc,           // Independence Tower: reveal 1 of their notes for 1 command token (option = the note, -1 don't)
    kMonAskCeldauri,      // Hyperlane Relay: 1 ground force to a dock planet (option = planet * 16 + type, -1 done)
    // (0.82.4) Discordant Stars, batch 3
    kMonAskKollecc,       // Shades' Den: captured units into its system (option = unit type, 100 all, -1 done)
    kMonAskBentor,        // Tucc Academy: DEPLOY on the planet just explored (1 yes, 0 no; Decision::planet)
    kMonAskKjalengard,    // Krotas Bannerhall: a command token for you and the winner (1 yes, 0 no; targetPlayer = the winner)
    kMonAskKjalengardWin, // ... asked of the winner: their token (1 yes, 0 no)
    kMonAskLanefir,       // Forbidden Library: its specialty instead of exhausting a planet (1 yes, 0 no)
    kMonAskKyro,          // Kyro Reliquary: the planet it goes to (option = planet)
    kMonAskKolume,        // Wonell's Bastion: fire (system * 16 + player) / start again (100000 + system) / -1 don't
    kMonAskGledge,        // Verdant Halo: who treats the laws as blank (option = player, -1 not now)
    kMonAskGledgeCores,   // ... the 2 planets with a Core token to exhaust
    kMonAskRhodun,        // Vault of New Phrad: flip to Reliquat Unleashed (1 yes, 0 no)
    kMonAskRhodunBack,    // Reliquat Unleashed: flip back at a combat start (1 yes, 0 no; targetPlayer = the opponent)
    kMonAskAtokera,       // (0.83.0 Blue Reverie) Deepmantle Interface: produce up to 1 unit in its system (1 yes, 0 no)
    kMonAskBelkosea,      // (0.84.0) Armageddon Project: the superweapon card for the control token (option = the superweapon)
    kMonAskBelkoseaUse,   // ... whose ability the monument card uses now (option = the superweapon, -1 cancel)
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
    DsbtGledge,  // fixes 28 (the Gledge review): Dhonraz Installations, the Reference card - an action (option = the planet)
    CrimsonCommander,  // 0.58.2: Ahk Siever after a combat - 1 gain a commodity, 2 turn one into a trade good, 0 neither
    MoveBreach,        // 0.59.0: all 7 breach tokens are out - the inactive breach to move (option = its system)
    YinCommander,      // (fixes 26, the Yin review) Brother Omar Omega: which infantry goes back (asked of a person)
    PrideOfKenara,     // (fixes 26, the Hacan review) the planet a traded Pride of Kenara planet's units move to (option = planet)
    NaaluMechTE,       // (fixes 27, the Naalu review) Iconoclast (Thunder's Edge): a mech when another player gains a relic
    SaarCommanderCount,  // (fixes 27, the Saar review) Rowl Sarrig: how many of the new fighters / infantry go to that dock
    DsReadFates,         // (fixes 28, the Augurs review) Read the Fates: "ACTION: Place this face up in your play area"
    DsSplinteringGale,   // (fixes 28, the Khrask review) Splintering Gale: the up to 2 ships that gain Sustain Damage (option = type*16+i)
    // Blue Reverie (0.83.0): the Atokera Legacy
    BrModularDesigns,  // option = ability*10000 + unit type*100 + location (0 space, planet+1; 99 anywhere in the system), -1 none
    BrOrdagka,         // option = the predicted outcome, kNoVote none
    BrMirrorshard,     // option = the system for 1 cruiser, -1 none
    BrMagruda,         // option = the planet, -1 none
    BrKapoko,          // yes / no
    BrTriWeave,        // option = the planet, -1 keep the note
    BrObelisk,         // yes / no
    BrDeepmantle,      // (Monuments+) yes / no
    // Blue Reverie (0.84.0): the Belkosea Allied States
    BrClassified,      // Classified Developments: option = the superweapon (0 Availyn .. 4 Mors), -1 cancel
    BrClassifiedWhere, // ... option = the planet (Decision::value = the superweapon), -1 cancel
    BrSuperweapon,     // "at any time" in a combat: 660 + superweapon, 666 Synchrony Matrix, 667 Trinity Stockpile, 668 Armageddon
                       // Project; -1 fight on, -2 not during this combat (targetPlayer = the opponent, value = the system)
    BrAvailyn,         // option = the system for up to 3 fighters, -1 cancel
    BrGrom,            // option = the system, -1 cancel
    BrMors,            // option = the system, -1 cancel
    BrSynchrony,       // option = 100 + superweapon card, 1000 + planet, 20000 + legendary card, 30000 + relic, 40000 + leader slot,
                       // 50000 breakthrough, 50001 monument card; -1 cancel
    BrTrinity,         // option = the planet, -1 cancel
    BrRazalka,         // option = the system, -1 don't (targetPlayer = the active player, value = the active system)
    BrSpike,           // yes / no
    BrAscendancy,      // yes / no
    BrCiphus,          // yes / no
    BrWardog,          // option = how many commodities are converted to trade goods
    BrSortie,          // yes / no (the holder of Mercenary Sortie)
    BrSortieTarget,    // option = the unit type destroyed (infantry / fighter)
    // Blue Reverie (0.85.0): the Pharad'n Order
    PharTombs,         // Ancient Empire (setup): pick-many, the planets for up to 14 tomb tokens
    PharNokash,        // Nokash: option = the player allowed to use it, -1 cancel
    PharNokashPlanet,  // ... that player's planet (all their infantry there), -1 don't
    PharAvhkanUnlock,  // the commander's unlocking ACTION: pick-many, 5 planets (2 infantry destroyed on each)
    PharAvhkan,        // Avhkan: yes / no (destroy 1 of the opponent's infantry; targetPlayer = the opponent, value = the planet)
    PharHero,          // Pharad'n the Immortal: 1 destroy all infantry there, 2 commit captured infantry, 0 no (value = the planet)
    PharHeroCount,     // ... how many captured infantry (option = the count)
    PharSacrotitan,    // Sacrotitan DEPLOY: yes / no (value = the planet)
    PharImmortal,      // Immortal II DEPLOY: option = the planet for 1 infantry, -1 none
    PharImpactor,      // Reality-Field Impactor: option = the anomaly, -1 none (value = the active system)
    PharDeathBinding,  // Death Binding (its holder): yes / no
    PharDeathRemove,   // ... pick-many: where the 4 infantry come from (planet ids; 100000 + system for space)
    PharDeathTech,     // ... option = the technology
    PharVaultsProduce, // Hidden Vaults: produce 1 infantry on the explored planet (yes / no)
    PharVaultsDestroy, // Hidden Vaults: pick-many units to destroy (planet*64 + type*... encoded), none = cancel
    PharPyramid,       // Black Pyramid DEPLOY: option = the planet, -1 no
    Count
};

// (0.81.0) What an Absol's Agendas question asks: Decision::value with Tag::AbsolChoice (AbsolTie: the outcome the votes were for,
// or kAbTieMostVP / kAbTieLeastVP; AbsolGiveTo: kAbPaxCeasefire or 0).
enum AbsolAsk : int {
    kAbNone = 0, kAbAbolishLaw, kAbBottomOutcome, kAbArbiter, kAbArticlesIgnore, kAbArticlesMechs, kAbChecksKeep, kAbDefenseExtra,
    kAbDefenseDestroy, kAbHyperShips, kAbAntiquities, kAbExploration, kAbPeace, kAbPolicyKeep, kAbPolicyGive, kAbWar, kAbNexusIgnore,
    kAbTravelIgnore, kAbPlaceTheirToken, kAbRevolutionTech, kAbRevolutionDestroy, kAbSchematicsGain, kAbSharedExplore, kAbSitesExplorePay,
    kAbSitesAgainst, kAbWarrantTake, kAbAgreementMech, kAbCrisisPrimary, kAbExecutionTake, kAbGrantGain, kAbMeasuresExhaust,
    kAbOperationsRemove, kAbRedistPlace, kAbRedistWho, kAbRedistInfantry, kAbReductionCruisers, kAbReductionPlace, kAbSwordsExplore,
    kAbWhGain, kAbWhDestroy, kAbNexusDestroy, kAbPaxProduce, kAbPaxCeasefire, kAbProphecyTake, kAbSpendMenu, kAbSpendSystem,
    kAbSpendStructure, kAbSpendExplore, kAbAntiquitiesFree, kAbPoliticalSecret,
    kAbTieMostVP = 5000, kAbTieLeastVP = 5001
};

struct Option {
    int id = 0;
    std::string label;
    std::string detail;
    bool enabled = true;
    int at = -1;  // 0.61.2: the system this choice is in (the screens let you pick it on the map); planet ids are found by themselves
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
    bool onSpots = false;    // 0.61.2: options are empty spaces at the edge of the board (id = spotId(hex)) - picked on the map

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
    bool reclaim = false;      // Produce: more than the reinforcements hold - the rest come back off the board (LRR 23.4a, fixes 32)
    int predicted = -1;        // VoteOutcome: the outcome this voter predicted (Radiance / Unity Algorithm, 0.44.6), -1 none
    int riderBet = -1;         // VoteOutcome / VotePlanets (0.65.0, for the AI): the outcome this voter's rider predicted
    int riderCard = -1;        // ... and that rider (action card), -1 none
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
