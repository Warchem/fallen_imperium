# Fallen Imperium

An unofficial fan-made digital version of **Twilight Imperium 4th Edition** for Windows, the Steam Deck
(Linux) and the Miyoo Mini Plus handheld, played against computer opponents (3 to 8 players, or hot-seat with
friends).

**Current version: 0.60.0 (Pre-Beta).** Downloads are on the [Releases](../../releases) page.

> Unofficial fan project. Twilight Imperium is a trademark of Fantasy Flight Games; this project is not
> affiliated with or endorsed by Fantasy Flight Games or Asmodee. It is free and not for sale.

## What's in it
- The base game and **Prophecy of Kings**, with **Codex**, **Uncharted Space**, **Thunder's Edge** (incl. the
  Fracture, breakthroughs and galactic events) and **Discordant Stars** as modules you switch on.
- All factions: the 24 of the base game and Prophecy of Kings, the Council Keleres, the Thunder's Edge factions
  and the 34 Discordant Stars factions, with their abilities, leaders, technologies, units and promissory notes.
- Computer players with personalities (archetypes, grudges, betrayals), a strategy core per faction, plans over
  several turns, and four difficulty levels.
- Negotiations: trades, promises, loans, threats and vote deals - offered over a Metal Gear-style comms codec,
  with an animated portrait and a voice for every faction.
- Three tilesets: **Retrowave** (animated pixel art, the default), **Voidfall** (painted, high definition) and
  **Simplistic** (clean drawn shapes); animated space and ground battles; galaxy drafting; mouse, keyboard and
  controller support.
- An animated opening: the name written in the Lazaxian alphabet, then a living world behind the title
  (Retrowave) or a battlefield at twilight (Voidfall).

## What's new since 0.59.30
The full list, version by version, is in `GUIDE.txt` (in the download, and `package/README.txt` here). Games
started with an older version keep their own rules, so old saves replay exactly as before.

- **0.60.0: the Voidfall title, the agenda and relic passes.**
  - With the Voidfall tileset the opening ends on a battlefield: troopers fighting from cover across a
    cratered plain, walkers trading Gatling lasers and beams and blowing up, warships overhead and a
    dreadnought's bombardment landing anywhere on the field. The opening's last swell blends into the start of
    the title song.
  - All 50 agendas were checked outcome by outcome against their cards (99 checks), and their texts are now
    the cards' wording. Fixed: Enforced Travel Ban and Nexus Sovereignty only stop movement through those
    wormholes (space cannon still fires through them and players joined by them are still neighbours);
    Miscount Disclosed keeps its law in play during the revote; Representative Government allows no extra
    votes; Wormhole Research and Ixthian Artifact let you decline to research; Committee Formation is used
    after the riders, just before the vote; space stations can't be elected as planets; and more.
  - All the relics were checked the same way. Fixed: The Codex, Neuraloop (the game no longer runs out of
    objectives a round early), Maw of Worlds, the Crown of Thalnos and the Stellar Converter. New: the three
    Codex II relics - **Dynamis Core**, **JR-XS455-O** and **Nano-Forge** - in games with the Codex or
    Thunder's Edge.
- **0.59.70: a new title screen and the last ten Discordant Stars factions.** The opening writes "Fallen
  Imperium" letter by letter in the Lazaxian alphabet, each letter shifting into English, then pans down to a
  nuclear-battered city world (any button skips it). The faction debug pass finished with the Nivyn, Nokar,
  Olradin, Roh'Dhna, Tnelis, Vaden, Vaylerian, Veldyr, Zealots of Rhodun and Zelian: all 64 factions have now
  been checked card by card.
- **0.59.40: twenty-four Discordant Stars factions** checked against the Discordant Stars Reference, one card
  at a time, from the Augurs of Ilyxum to the Myko-Mentori. Agents and abilities that work "for any player"
  now ask a person each time (with "don't ask again this round"); the two-step strategy cards say which step
  you are on.

Before that, 0.59.0 - 0.59.30 checked the 30 factions of the base game, Prophecy of Kings, the Council Keleres
and Thunder's Edge, the shared technologies and the 93 action cards against the cards' exact wording, and
0.58.x added "Hyperlanes: On / Off" (the rulebook's printed boards for every player count).

## Playing
- **Windows:** unzip `TI4-Windows-<version>.zip` and run `TwilightImperium.exe`.
- **Steam Deck / Linux:** unpack `TI4-SteamDeck-<version>.tar.gz` and run `TwilightImperium.sh` (on the Deck you
  can add it to Steam as a non-Steam game). See `README.txt` inside the download.
- **Miyoo Mini Plus (OnionOS):** unzip `TI4-Miyoo-<version>.zip` onto the SD card (it goes into
  `App/TwilightImperium`) and start it from the Apps list.
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
