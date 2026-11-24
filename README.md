# Fallen Imperium

An unofficial fan-made digital version of **Twilight Imperium 4th Edition** for Windows, the Steam Deck
(Linux) and the Miyoo Mini Plus handheld, played against computer opponents (3 to 8 players, or hot-seat with
friends).

**Current version: 0.86.0 (Pre-Beta).** Downloads are on the [Releases](../../releases) page.

> Unofficial fan project. Twilight Imperium is a trademark of Fantasy Flight Games; this project is not
> affiliated with or endorsed by Fantasy Flight Games or Asmodee. It is free and not for sale.

## What's in it
- The base game and **Prophecy of Kings**, with **Codex**, **Uncharted Space**, **Thunder's Edge** (incl. the
  Fracture, breakthroughs and galactic events) and **Discordant Stars** as modules you switch on - and three
  fan-made ones: **Absol's Agendas**, **Monuments+** and **Blue Reverie**.
- All factions: the 24 of the base game and Prophecy of Kings, the Council Keleres, the Thunder's Edge factions
  and the 34 Discordant Stars factions, with their abilities, leaders, technologies, units and promissory notes -
  plus the Blue Reverie factions (the Atokera, the Belkosea and the Pharad'n so far), secret factions you
  unlock in the Career.
- Computer players with personalities (archetypes, grudges, betrayals), a strategy core per faction, plans over
  several turns, a sense of when a rival is really coming for what they hold, and five difficulty levels - the
  top one, A.I., thinks ahead by playing its options out.
- **The Career:** a ladder of 20 leagues, each a season of games against named rival commanders who remember
  you, with a faction draft, league points and bonuses, emblems, faction colours and secret factions to earn;
  beaten rivals can join your Casual games. Your progress lives in a profile folder that every new version
  finds.
- Negotiations: trades, promises, loans, threats and vote deals - offered over a Metal Gear-style comms codec,
  with an animated portrait and a voice for every faction.
- Three tilesets: **Retrowave** (animated pixel art, the default), **Voidfall** (painted, high definition) and
  **Simplistic** (clean drawn shapes); animated space and ground battles; galaxy drafting; mouse, keyboard and
  controller support.
- An animated opening: the name written in the Lazaxian alphabet, then a living world behind the title
  (Retrowave) or a battlefield at twilight (Voidfall).
- Sound: the soundtrack, synthesised battle and map sounds, a voice for every faction over the comms, and a music
  player for your own MP3s.

## What's new since 0.80.0
The full list, version by version, is in `GUIDE.txt` (in the download, and `package/README.txt` here). Games
started with an older version keep their own rules, so old saves replay exactly as before.

- **0.86.0: bots that defend what they hold.** Before it moves, a Competitive bot works out what each rival with
  ships nearby would do on its own turn, and which fleet a rival is gathering toward it over several turns. It
  guards a system - Mecatol Rex, its home, its best planets - only when one of those plans attacks it, when the
  guard would really change the fight, and when what it would lose is worth more than its other moves. Also:
  voices for the Atokera, the Belkosea and the Pharad'n over the comms.
- **0.85.x: the Pharad'n Order and secret factions.** Blue Reverie's third faction: an undying empire that pays
  for its infantry with captured soldiers, its tombs on planets across the galaxy. The Blue Reverie factions are
  now secret: three new advanced leagues in the Career (Living Forge Synod, Arsenal Directive, Necropolis
  Eternal) each unlock one, and nobody plays one until it is unlocked. Your Career progress, unlocks, options and
  autosave now live in a profile folder outside the game's folder (Windows: Saved Games\Fallen Imperium; Steam
  Deck: ~/.local/share/Fallen Imperium), brought over from an older version on the first start.
- **0.83.0 - 0.84.1: Blue Reverie** (a fan-made module by TacticBlue): the Atokera Legacy (living metal that
  turns every world it holds into a forge) and the Belkosea Allied States (an arms race of five superweapons),
  everything on their cards with the cards' exact wording; a Send Offer button always in view on the offer screen.
- **0.82.x: Monuments+** (a fan-made module by Cacotopos, after Blarknob's Monuments): a monument for every
  faction, raised with the reworked Construction card, each with its own ability; a Monuments screen; 3 action
  cards, 2 agendas and 2 secret objectives.
- **0.81.x: Absol's Agendas** (a fan-made deck by Absol197): 60 cards that replace the agenda deck, with new
  kinds of votes and laws that carry tokens; scroll bars on long lists; the mouse wheel scrolls lists.
- **0.80.11 and 0.80.1: the Career.** The Tournament is now the Career; beat a rival commander in a league and
  they can join your Casual games, holding the grudges of your league games. From the GitHub issues: the secret
  objective you keep at the start is picked from two cards with a confirming second click, and the Vuil'raith
  can build with their own captured units again.

Before that, 0.80.0 brought the Tournament (league mode); 0.61 - 0.75 the cards' own words, 24 faction colours,
the A.I. difficulty, the War Sun / Fracture / Obsidian scenes, map and comms sounds, combat reactions, your own
key bindings and the music player; 0.59 - 0.60 checked every faction and card against its printed text.

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
- Fan-made modules, used with thanks to their makers: **Absol's Agendas** by Absol197, **Monuments+** by
  Cacotopos (after Blarknob's Monuments) and **Blue Reverie** by TacticBlue.
- DejaVu fonts (Bitstream Vera licence); the Lazaxian font supplied by the project owner.
- [stb](https://github.com/nothings/stb) image / truetype (public domain), [minimp3](https://github.com/lieff/minimp3)
  (CC0), SDL2 headers (zlib licence, `third_party/SDL2`), SDL 1.2 headers (LGPL 2.1, `third_party/SDL12`).
