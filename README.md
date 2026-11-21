# Fallen Imperium

An unofficial fan-made digital version of **Twilight Imperium 4th Edition** for Windows, the Steam Deck
(Linux) and the Miyoo Mini Plus handheld, played against computer opponents (3 to 8 players, or hot-seat with
friends).

**Current version: 0.80.0 (Pre-Beta).** Downloads are on the [Releases](../../releases) page.

> Unofficial fan project. Twilight Imperium is a trademark of Fantasy Flight Games; this project is not
> affiliated with or endorsed by Fantasy Flight Games or Asmodee. It is free and not for sale.

## What's in it
- The base game and **Prophecy of Kings**, with **Codex**, **Uncharted Space**, **Thunder's Edge** (incl. the
  Fracture, breakthroughs and galactic events) and **Discordant Stars** as modules you switch on.
- All factions: the 24 of the base game and Prophecy of Kings, the Council Keleres, the Thunder's Edge factions
  and the 34 Discordant Stars factions, with their abilities, leaders, technologies, units and promissory notes.
- Computer players with personalities (archetypes, grudges, betrayals), a strategy core per faction, plans over
  several turns, and five difficulty levels - the top one, A.I., thinks ahead by playing its options out.
- **The Tournament:** a ladder of 17 leagues, each a season of games against named rival commanders who
  remember you, with a faction draft, league points and bonuses, emblems and faction colours to earn.
- Negotiations: trades, promises, loans, threats and vote deals - offered over a Metal Gear-style comms codec,
  with an animated portrait and a voice for every faction.
- Three tilesets: **Retrowave** (animated pixel art, the default), **Voidfall** (painted, high definition) and
  **Simplistic** (clean drawn shapes); animated space and ground battles; galaxy drafting; mouse, keyboard and
  controller support.
- An animated opening: the name written in the Lazaxian alphabet, then a living world behind the title
  (Retrowave) or a battlefield at twilight (Voidfall).
- Sound: the soundtrack, synthesised battle and map sounds, a voice for every faction over the comms, and a music
  player for your own MP3s.

## What's new since 0.60.0
The full list, version by version, is in `GUIDE.txt` (in the download, and `package/README.txt` here). Games
started with an older version keep their own rules, so old saves replay exactly as before.

- **0.80.0: the Tournament (league mode).** Title screen > Tournament: 17 leagues, each a season of games
  against the same named rival commanders. Places earn league points (6 / 4 / 3 / 1, shared places split), plus
  bonuses (take Mecatol Rex first, win before round 7, finish above your rival, keep every promise, knock a
  player out, ...). Reach the target first to win the league - its emblem and its faction colour - and open the
  next ones; lose the race and it starts again. A faction draft before most games (the last to pick is the
  speaker); rivals remember who finished above whom and who broke a promise, and bring a grudge to the next game.
  Each league keeps its own save. Options > "Unlock everything (testing)" opens it all.
- **0.75.0 and 0.62.0: 24 faction colours.** Gold, Silver, Bronze, Luminescent Green, Hatred Crimson,
  Pearlescent, Ion Cyan, Void, Brown, Retro Sunset, Crystal, Ash, Prismatic, Wireframe Green / Blue and Rose Gold
  beside the classic eight - most of them animated. The new ones are earned in the Tournament.
- **0.74.0: the A.I. difficulty (experimental).** No personality; before its big choices it plays its best
  options out on copies of the game (re-dealing every card it can't see - it never peeks) and takes the best.
- **0.73.x: scenes.** The Age of the War Sun (the first war sun technology of a game), the Fracture tearing open
  in a ripple across the map, and the Obsidian unmasked.
- **0.69.0 - 0.72.0: sound and controls.** Map sounds (sirens, invasions, every faction's ships on the move),
  sounds for the big moments and the comms screen, a voice for every faction, combat reactions (both sides speak
  as a fight starts), a gold edge on whatever the pointer can click, Shift to speed the camera, edge scrolling,
  and your own key bindings.
- **0.68.0: the music player.** Put your own MP3s in "My Music"; games play the soundtrack, your music or both.
- **0.63.0 - 0.67.0: screens and bots.** A new New Game screen (Players, Factions & seats, VP, Galaxy with a
  typed seed and "Fair slices", Game options), the computer players' opinion of you on the offer screen, bots
  that plan their votes for the whole agenda phase, a first balance pass on the weakest factions, and the screen
  buttons moved into the top banner.
- **0.61.x: the cards' own words.** Every objective, exploration card, strategy card, note, legendary planet,
  action card and galactic event now shows its card's text, with the rules checked against each; taking a unit
  back from the board when none is left (Living Rules 23.4a); picking planets on the map; DirectX drawing on
  Windows (no tearing).

Before that, 0.59.0 - 0.60.0 checked every faction, the shared technologies, the action cards, the agendas and
the relics against their cards, and 0.58.x added "Hyperlanes: On / Off".

## Playing
- **Windows:** unzip `TI4-Windows-<version>.zip` and run `TwilightImperium.exe`.
- **Steam Deck / Linux:** unpack `TI4-SteamDeck-<version>.tar.gz` and run `TwilightImperium.sh` (on the Deck you
  can add it to Steam as a non-Steam game). See `README.txt` inside the download.
- **Miyoo Mini Plus (OnionOS):** when a release has a Miyoo build, unzip `TI4-Miyoo-<version>.zip` onto the SD
  card (it goes into `App/TwilightImperium`) and start it from the Apps list. (The PC versions come first now;
  not every release has a Miyoo build.)
- `GUIDE.txt` (in the download, and `package/README.txt` here) explains the controls and lists what is new in
  each version.

The downloads include the soundtrack by **Michael Keane** (see Credits). The music files are not in this source
repository; a build from source plays without music unless you put the MP3s next to the game (any MP3 named
`music.mp3` works too).

## Building from source
Linux (x86-64) with a C++17 compiler and Python 3 (numpy, scipy, Pillow for the art tools):

```sh
make host            # the PC build for testing: build/host/ti4
make test            # automated screen tests and simulated games (screenshots in build/test)
make windows-toolchain && make windows-package   # dist/TI4-Windows-<version>.zip
make linux-package                                 # dist/TI4-SteamDeck-<version>.tar.gz
make toolchain && make package                     # dist/TI4-Miyoo-<version>.zip (Miyoo Mini Plus)
```

`ti4 --simulate N` plays N computer-only games and checks every answer against the rules.

## Credits and licences of included parts
- Music by **Michael Keane** - *The Council Keleres* and *Infiltration* (Embers of the Imperium), *Action*
  (Twilight Symposium). The composer made them for public use and asks only for credit; he is also named on
  the game's Credits screen.
- Game data (tiles, planets, units, factions, objectives) and card wording generated from the
  [AsyncTI4](https://github.com/AsyncTI4/TI4_map_generator_bot) bot's data set.
- Discordant Stars factions: the Discordant Stars fan expansion and its authors.
- DejaVu fonts (Bitstream Vera licence); the Lazaxian font supplied by the project owner.
- [stb](https://github.com/nothings/stb) image / truetype (public domain), [minimp3](https://github.com/lieff/minimp3)
  (CC0), SDL2 headers (zlib licence, `third_party/SDL2`), SDL 1.2 headers (LGPL 2.1, `third_party/SDL12`).
