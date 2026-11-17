# Negotiation lines - writing brief

Fallen Imperium (a digital Twilight Imperium 4 fan project) shows the computer players' speech in a Metal Gear
Solid-style "comms codec": the bot's animated portrait, then its line typing out in a speech box above the deal
terms (the terms themselves are shown separately, so lines never need to state them). The owner (who does not
code) described each faction's voice in `docs/owner/negotiation/Negotiation_Line_Gists.txt` - READ IT FIRST and
follow it exactly; his notes below override anything else.

## Owner's extra notes (they override the gists)
- L1Z1X: brutalist cybernetic Lazax who still call themselves the Lazax; everyone else is a subjugated pawn.
  Write the line in plain English only - the game wraps it in electronic junk with Lazaxian glyphs at both ends.
- Nekro Virus: every line is EXACTLY two readable words written as `Word | Word` (e.g. `Trade | Accepted`,
  `Flesh | Obsolete`). The game buries them in machine junk. Nothing else on the line.
- Vuil'raith Cabal: speaks as "we" but is NOT a hivemind; malevolent and seductive in an eldritch way (never
  sexual); the galaxy is their next meal. Never mention "the Tear".
- L'tokk Khrask: ancient, gravelly, slow like stone, sage advice - about preserving nature / the living world.
- Monks of Kolume: AI robots - introspective, space-Buddhist reflections on life and the state of the universe.
  NO stoner talk at all (the gist mentions it - the owner removed it).
- Naalu: pompous nagas who look down on everyone; a hissing voice - drawn-out s sounds ("Ssso", "yesss") in some
  words (not every word; keep it readable).
- Arborec: a hivemind - always "we", never "I"; blunt, as if it cannot understand emotions; may call itself
  "The Green"; a "leafy" voice - an occasional rustle ("...shhhrrr...") is welcome but sparing.
- Obsidian (the Firmament after it is possessed by eldritch things from beyond the void): hostile, chaotic, evil.
  The game shows ALL Obsidian lines in the Lazaxian alphabet, which only has: A-Z a-z 0-9 space ! " ' ( ) , - . ?
  -> Obsidian lines may use ONLY those characters (no colon, semicolon, dash other than -, ellipsis character, etc).
- Firmament: peaceful, cult-like protectors of the galaxy (five races in one council - they may say "we").
- Ghemina Raiders: two speakers - the Lord leads, the Lady finishes. Write `Lord's part || Lady's part`
  (e.g. `We consider the terms of the deal... || Non-negotiable.`).
- Zelian Purifiers: a sentient asteroid; rock grating on rock. Start with a growl, then a [bracketed translation]:
  `rrghrghkkkrgh... [This trade is acceptable.]`
- Glimmer of Mortheus: telepathic, cosmic riddles (the game fades the words in and out).
- Roh'Dhna: speaks like a manual or a law, citing made-up doctrine codes ("Trade Doctrine 67-52P states...").
- Keleres: space-police jargon, straight to the point; hostility met with self-defence threats ("Calling in a
  10-33! All units, standby for planetary assault.").
- Nomad: speaks singularly ("I") - he is everywhere at once; proud urgency to stop a terrible future.
- Expressions: no swearing; PG-13 at most; no real-world slurs or politics beyond the owner's own words.

## Factions the owner was unsure about
Invent a fitting voice from what the faction is (read its entry in `docs/owner/TI4-Faction-Strategy-Guides.txt`
and the short blurb in `src/app/gen/faction_blurbs.inc`), and put your one-line voice summary in the `voice:`
field so the owner can review it. Prefer attitude and manner of speech over invented lore facts; don't state
specific history you are not sure of.

## What to write - per faction, in this order
```
[key]
name: <how OTHER factions address this faction in speech: short, e.g. Hacan, Barony, Star Kings>
voice: <one line: the voice you wrote (for the owner to review)>
offer: <3 lines - the bot sends an offer>
offer: ...
offer: ...
friendly: <1 line - an offer to a partner it likes / trusts>
threat: <1 line - an offer with a threat or a demand attached ("leave that system or else")>
vote: <1 line - an offer about votes in the Galactic Council (agenda phase)>
accept: <2 lines - it accepts the player's offer>
accept: ...
decline: <2 lines - it refuses the player's offer>
decline: ...
counter: <1 line - it refuses as offered but sends its own terms back>
betray: <1 line - it breaks a promise to / turns on {them}; spoken TO the victim>
betrayed: <1 line - its answer when {them} betrays it; spoken TO the betrayer>
yield: <2 lines (owner 2026-11-15) - it gives in to {them}'s threat and pays: grudging, never pleased; it will remember>
yield: ...
```
- `{them}` is replaced by the OTHER faction's `name:` (the listener). Use it in at least one offer line and in
  both betray / betrayed lines (they must name the other side). Elsewhere optional, at most once per line.
- Lines must fit ANY partner and ANY deal (trade goods, commodities, promissory notes, promises of peace,
  votes, leaving a system...). Don't name specific items, planets or numbers.
- Length: 1-2 sentences, at most 160 characters including {them} (count it as 12). Most around 80-130.
- Variety: don't start the lines of a faction the same way; each line should sound like THAT faction.
- Never quote card text, flavour text or rulebook text from the real game; write your own.
- Plain ASCII apart from the ellipsis "..." (three dots). Use " - " for a dash.
- Free names you may use: Mecatol Rex, the Council (Galactic Council), the Lazax, the Twilight Wars, the galaxy,
  trade goods, commodities, votes, fleets, the throne.

Write the file named in your task under `tools/lines/` (UTF-8, Unix newlines), sections in the order given,
one blank line between sections. `#` starts a comment line. Then reread it once: fix any line over 160 chars,
any missing {them} in betray / betrayed, any Nekro line that isn't exactly `Word | Word`, any Ghemina line
without `||`, any Obsidian character outside the allowed set.
