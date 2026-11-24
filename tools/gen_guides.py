#!/usr/bin/env python3
"""Strategy Core data from the owner's faction strategy guide (0.43.0).

Reads docs/owner/TI4-Faction-Strategy-Guides.txt (pdftotext -layout of the owner's
TI4-Faction-Strategy-Guides.pdf, 64 factions, a Prophecy of Kings plan and a Thunder's
Edge plan each) and writes src/ai/gen/guides.inc (the bots' Strategy Core table) and
src/app/gen/faction_blurbs.inc (the faction picker's summary, difficulty and tags).

What can't be read reliably from prose is written here by hand: the strategy cards each
plan wants early and late (CARDS). The values the guide doesn't speak to (the second
technology paths and the behaviour dials of the 0.18-0.42 table) come from
tools/guide_old.json, a snapshot of that table, blended with the guide's style tags.

(0.83.0) NOT reproducible any more: running it today changes the other factions' technology paths (the guide text or
the table were adjusted after 0.43.0). New factions (HAND, Blue Reverie) are appended to src/ai/gen/guides.inc and
src/app/gen/faction_blurbs.inc by hand from this script's output - don't overwrite those files with a full run.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDE = os.path.join(ROOT, 'docs/owner/TI4-Faction-Strategy-Guides.txt')

TOC = ("The Arborec|The Ghosts of Creuss|The Emirates of Hacan|The Universities of Jol-Nar|The L1Z1X Mindnet|"
       "The Barony of Letnev|The Mentak Coalition|The Embers of Muaat|The Naalu Collective|The Nekro Virus|The Clan of Saar|"
       "The Sardakk N'orr|The Federation of Sol|The Winnu|The Xxcha Kingdom|The Yin Brotherhood|The Yssaril Tribes|"
       "The Argent Flight|The Vuil'raith Cabal|The Empyrean|The Mahact Gene-Sorcerers|The Naaz-Rokha Alliance|The Nomad|"
       "The Titans of Ul|The Council Keleres|The Last Bastion|The Deepwrought Scholarate|The Firmament|The Ral Nel Consortium|"
       "The Crimson Rebellion|The Shipwrights of Axis|The Bentor Conglomerate|The Celdauri Trade Confederation|"
       "The Cheiran Hordes|The Savages of Cymiae|The Dih-Mohn Flotilla|The Edyn Mandate|The Florzen Profiteers|"
       "The Free Systems Compact|The Ghemina Raiders|The Ghoti Wayfarers|The GLEdge Union|The Augurs of Ilyxum|"
       "The Berserkers of Kjalengard|The Kollecc Society|The Monks of Kolume|The Kortali Tribunal|The Kyro Sodality|"
       "The Lanefir Remnants|The Li-Zho Dynasty|The L'tokk Khrask|The Mirveda Protectorate|The Glimmer of Mortheus|"
       "The Myko-Mentori|The Nivyn Star Kings|The Nokar Sellships|The Olradin League|The Roh'Dhna Mechatronics|"
       "The Zealots of Rhodun|The Tnelis Syndicate|The Vaden Banking Clans|The Vaylerian Scourge|The Veldyr Sovereignty|"
       "The Zelian Purifier").split('|')
KEYS = ("arborec ghost hacan jolnar l1z1x letnev mentak muaat naalu nekro saar sardakk sol winnu xxcha yin yssaril argent "
        "cabal empyrean mahact naaz nomad titans keleres bastion deepwrought firmament ralnel crimson axis bentor celdauri "
        "cheiran cymiae dihmohn edyn florzen freesystems ghemina ghoti gledge augers kjalengard kollecc kolume kortali kyro "
        "lanefir lizho khrask mirveda mortheus mykomentori nivyn nokar olradin rohdhna zealots tnelis vaden vaylerian "
        "veldyr zelian").split()
HEADS = ["Faction kit at a glance", "PROPHECY OF KINGS GAME", "Game plan", "Opening (rounds 1–2)", "Strategy cards",
         "Tech path", "Leaders and mech", "Mid and late game", "Watch out for", "Playing against them",
         "PROPHECY OF KINGS + THUNDER'S EDGE GAME", "BREAKTHROUGH", "What changes with Thunder's Edge", "Adjusted plan",
         "Expedition priority"]

# strategy cards: 0 Leadership 1 Diplomacy 2 Politics 3 Construction 4 Trade 5 Warfare 6 Technology 7 Imperial
# (early = rounds 1-2 best first, late = round 4 on, avoid = rounds 1-2) - read from each guide's "Strategy cards"
CARDS = {
    'arborec': ([3, 0], [2, 7], []), 'ghost': ([6, 0, 1, 5], [7], []), 'hacan': ([4, 2], [7, 6], []),
    'jolnar': ([6, 2, 0], [7], []), 'l1z1x': ([5, 0], [7], []), 'letnev': ([5, 0, 6], [7], []),
    'mentak': ([4, 2, 5], [], []), 'muaat': ([0, 5], [7], []), 'naalu': ([6, 7], [7], []),
    'nekro': ([5, 0, 2], [], []), 'saar': ([3, 0, 5, 4], [7], []), 'sardakk': ([5, 0, 6], [], []),
    'sol': ([0, 6], [7], []), 'winnu': ([0, 2], [7], []), 'xxcha': ([1, 2, 6], [7], []), 'yin': ([5, 0, 6], [2, 7], []),
    'yssaril': ([2, 6, 0], [7], []), 'argent': ([5, 0, 2, 6], [], []), 'cabal': ([5, 0, 3], [], []),
    'empyrean': ([1, 2, 0], [], [5]), 'mahact': ([5, 0], [2, 7], []), 'naaz': ([3, 0, 6], [7, 2], []),
    'nomad': ([2, 0, 5], [7], []), 'titans': ([3, 0, 6], [], []), 'keleres': ([2, 0], [7], []),
    'bastion': ([5, 0, 3], [7], []), 'deepwrought': ([6, 1, 2, 4], [], []), 'firmament': ([2, 7, 0], [], []),
    'ralnel': ([3, 2, 0, 5], [], []), 'crimson': ([5, 0, 6], [7], []), 'axis': ([4, 3], [2, 7], []),
    'bentor': ([4, 3, 6], [2], []), 'celdauri': ([3, 4, 0], [], []), 'cheiran': ([5, 0, 3], [7], []),
    'cymiae': ([2, 0, 5], [], []), 'dihmohn': ([0, 5, 6], [], []), 'edyn': ([2, 7, 5, 1], [], []),
    'florzen': ([3, 4, 6, 2], [], []), 'freesystems': ([1, 3, 0, 2], [], []), 'ghemina': ([5, 0], [], [3]),
    'ghoti': ([0, 5], [], []), 'gledge': ([3, 0], [7], [4]), 'augers': ([7, 6, 0], [7], []),
    'kjalengard': ([5, 0], [], [6]), 'kollecc': ([3, 0, 5, 6], [], []), 'kolume': ([0, 6, 2, 1], [], []),
    'kortali': ([5, 0], [], []), 'kyro': ([2, 0], [2], []), 'lanefir': ([6, 5, 3], [], []), 'lizho': ([0, 6, 3], [], []),
    'khrask': ([2, 3, 5], [], []), 'mirveda': ([0, 6, 3], [], []), 'mortheus': ([2, 1, 3], [], []),
    'mykomentori': ([3, 4, 0, 2], [], []), 'nivyn': ([5, 0, 6], [], []), 'nokar': ([5, 0, 1, 4], [], []),
    'olradin': ([3, 0], [2], []), 'rohdhna': ([3, 0, 5], [], []), 'zealots': ([6, 2, 5], [], []),
    'tnelis': ([2, 7, 0], [7], []), 'vaden': ([6, 4, 0, 2], [], []), 'vaylerian': ([5, 2, 0], [], []),
    'veldyr': ([4, 1, 6], [], []), 'zelian': ([5, 0, 6], [], []),
}
# Thunder's Edge hints, read from each guide's "What changes" / "Adjusted plan": the Fracture (+1 go in, +0.5 when it
# suits, -1 defend the ingress instead), from which round, and space stations (+1 take them early, +0.5 useful,
# -0.5 matter less)
TE = {
    'arborec': (-1, 0, 0.5), 'hacan': (0.5, 3, 1), 'l1z1x': (1, 0, 0), 'letnev': (1, 0, 0), 'saar': (0.5, 0, 0),
    'sardakk': (1, 0, 0), 'sol': (0.5, 0, 0), 'xxcha': (0, 0, -0.5), 'cabal': (1, 0, 0), 'mahact': (0.5, 0, 0),
    'naaz': (0.5, 0, 0), 'bastion': (1, 0, 0), 'deepwrought': (0, 0, 0.5), 'ralnel': (0, 0, 0.5), 'obsidian': (1, 0, 0),
    'axis': (0, 0, 1), 'bentor': (1, 0, 0.5), 'celdauri': (0.5, 0, 0.5), 'cheiran': (1, 3, 0), 'dihmohn': (0.5, 0, 0.5),
    'florzen': (0.5, 0, 0), 'freesystems': (0.5, 0, 0), 'ghemina': (1, 0, 0), 'ghoti': (0, 0, 1), 'gledge': (0.5, 0, 0.5),
    'kjalengard': (1, 0, 0), 'kollecc': (0.5, 0, 0), 'kolume': (0.5, 0, 0), 'kortali': (1, 0, 0), 'kyro': (0.5, 0, 0),
    'lanefir': (1, 0, 0), 'lizho': (0.5, 0, 0), 'khrask': (0.5, 0, 0), 'mirveda': (0.5, 0, 0), 'mortheus': (0, 0, 0.5),
    'mykomentori': (0, 0, 1), 'nivyn': (1, 0, 0), 'nokar': (0.5, 0, 0), 'olradin': (0.5, 0, 0), 'rohdhna': (1, 0, 0),
    'vaden': (0.5, 0, 0.5), 'vaylerian': (1, 0, 0), 'veldyr': (0, 0, 0.5), 'zelian': (1, 0, 0),
}
SLICE = {'res': 0, 'inf': 1, 'ac': 2, 'secret': 3, 'skip': 4, 'tg': 5}

# Blue Reverie (0.83.0): factions the owner's guide doesn't cover - written by hand from their kit, appended after the
# guide's factions. Row: early / late / avoid cards, core / aggressive / control / Thunder's Edge techs, the 13 numbers
# (dread cruiser destroyer fighter carrier infantry mech warSun | mecatol aggression expansion deals threat), expedition
# pay, from, hit early, (fracture, from, stations); blurb: difficulty, tags, text.
HAND = {
    'atokera': {
        'row': ([2, 0, 6], [7, 2], [],
                ['st', 'nm', 'cr2', 'pi', 'dsatoky', 'ie', 'gd', 'dn2'],
                ['cr2', 'dn2', 'asc', 'gd', 'pds2'], ['pi', 'dsatoky', 'ie', 'pds2', 'sdn'], ['pds2'],
                [1.0, 1.4, 1.0, 0.9, 1.0, 1.0, 1.2, 0.9, 1.0, 0.95, 1.1, 1.1, 1.0], [0, 1, 5], 0, 1.0, (0, 0, 0.5)),
        'blurb': ('INTERMEDIATE', 'Politics, Production, Defense',
                  'Living metal that turns every world it holds into a forge. The Atokera produce from any readied planet, '
                  'shield their worlds against bombardment and profit from the agenda phase - predictions, extra votes and '
                  'cruisers that deploy after the outcomes they backed.'),
    },
    'belkosea': {  # (0.84.0)
        'row': ([6, 0, 5], [7, 5], [],
                ['ps', 'ff2', 'gd', 'dsbelky', 'nm', 'dd2', 'cv2', 'dn2'],
                ['ff2', 'gd', 'dd2', 'cv2', 'dn2'], ['dsbelky', 'nm', 'pds2', 'ps', 'sdn'], ['dd2'],
                [1.0, 1.0, 1.0, 1.4, 1.3, 1.0, 1.0, 0.9, 1.0, 1.1, 1.0, 0.9, 1.1], [0, 1, 5], 0, 1.0, (0, 0, 0.5)),
        'blurb': ('ADVANCED', 'Superweapons, Fighters, Combat',
                  'A military alliance that builds superweapons on its worlds - a nebula that launches fighters, a gun that '
                  'fires along a line of systems, a field that strips its enemies\' sustain, a barrage, a cannon that damages a '
                  'whole fleet far away. Wyvern fighters land with the troops, and an ace turns their barrage on any ship.'),
    },
    'pharadn': {  # (0.85.0)
        'row': ([3, 0, 5], [7, 5], [],
                ['dspharinf', 'nm', 'dxa', 'gd', 'dspharb', 'sdn', 'pi', 'dn2'],
                ['dspharinf', 'gd', 'dxa', 'asc', 'dn2'], ['dspharinf', 'nm', 'pi', 'sdn', 'dspharb'], ['dxa'],
                [1.0, 1.0, 0.9, 0.8, 1.2, 1.3, 1.3, 0.9, 1.0, 1.1, 1.25, 0.9, 1.0], [0, 1, 5], 0, 1.0, (0, 0, 0.5)),
        'blurb': ('ADVANCED', 'Ground Forces, Expansion, Attrition',
                  'An undying empire waking in tombs across the galaxy. The Pharad\'n pay for their infantry with the soldiers '
                  'they capture - and they capture their own fallen. Every tomb they open brings 2 more, their dead rise again '
                  'as mechs, and their flagship flies faster the more troops it carries.'),
    },
}


def parse(path):
    txt = [l for l in open(path, encoding='utf-8').read().split('\n') if 'TI4 Faction Strategy Guides ·' not in l]
    starts = []
    for i, l in enumerate(txt):
        s = l.strip()
        for n in TOC:
            if s.startswith(n) and any(re.search(r'(BEGINNER|INTERMEDIATE|ADVANCED|EXPERT)', txt[j])
                                       for j in range(i, min(i + 3, len(txt)))):
                if not starts or starts[-1][0] != n:
                    starts.append((n, i))
    out = {}
    for k, (n, i) in enumerate(starts):
        end = starts[k + 1][1] if k + 1 < len(starts) else len(txt)
        block = txt[i:end]
        raw = '   '.join(x.strip() for x in block[0:3])
        hdr = re.sub(r'\s+', ' ', raw)
        sec, cur = {'intro': []}, 'intro'
        for l in block:
            s = l.strip()
            hit = next((h for h in HEADS if s == h), None)
            if hit:
                cur = hit
                sec.setdefault(cur, [])
                continue
            sec.setdefault(cur, []).append(s)
        sec = {kk: re.sub(r'\s+', ' ', ' '.join(x for x in vv if x)).strip() for kk, vv in sec.items()}
        diff = re.search(r'(BEGINNER-FRIENDLY|INTERMEDIATE|ADVANCED|EXPERT)', hdr)
        tags = re.sub(r'.*?(BEGINNER-FRIENDLY|INTERMEDIATE|ADVANCED|EXPERT)\s*', '', raw)
        tags = re.sub(r'\s*COMMODITIES.*', '', tags).strip()
        tags = ', '.join(t.strip() for t in re.split(r'\s{3,}', tags) if t.strip())
        out[KEYS[TOC.index(n)]] = {'difficulty': diff.group(1) if diff else '', 'tags': tags, 'sections': sec}
    return out


def tech_table():
    s = open(os.path.join(ROOT, 'src/engine/gen/gamedata.inc')).read()
    i = s.find('const TechDef kTechs[')
    j = s.find('};', i)
    rows = re.findall(r'\{"([^"]+)", "([^"]+)", TechColor::(\w+)', s[i:j])
    return sorted([(name, key) for key, name, _ in rows], key=lambda x: -len(x[0]))


def techs_in(text, tn, skip_start=True):
    found, t = [], text
    for name, key in tn:
        for m in re.finditer(re.escape(name), t):
            found.append((m.start(), key, name))
        t = t.replace(name, '#' * len(name))
    found.sort()
    out = []
    for pos, key, name in found:
        if skip_start and '(start' in text[pos + len(name):pos + len(name) + 9]:
            continue
        if key not in out:
            out.append(key)
    return out


def blurb(intro, name):
    # the summary paragraph: whole sentences up to ~210 characters, starting after the kit box
    # (home, commodities, starting techs and fleet), at the first "The ..." or "<name> ..." that reads as prose
    cut = max(intro.rfind('STARTING FLEET'), intro.rfind('STARTING TECH'))
    rest = intro[cut:] if cut >= 0 else intro
    toks = rest.split(' ')
    words = [w for w in name.replace("'", ' ').split(' ') if w not in ('The', 'of')]
    first = name.split(' ')[1] if name.startswith('The ') else name.split(' ')[0]
    start = None
    for i, t in enumerate(toks):
        if t == 'The' and any(x.split("'")[0] in words or x in words for x in toks[i + 1:i + 3]):
            start = i
            break
    if start is None:
        for i, t in enumerate(toks):
            if t == first and i + 1 < len(toks) and not re.match(r'^[(0-9]', toks[i + 1]):
                start = i
                break
    sents = re.split(r'(?<=[.!?])\s+', ' '.join(toks[start or 0:]))
    out = ''
    for s in sents:
        if not s:
            continue
        if out and (len(out) + len(s) > 235 and len(out) >= 110):
            break
        out = (out + ' ' + s).strip()
        if len(out) >= 235:
            break
    if len(out) > 245:  # three lines on the picker: cut at a word
        out = out[:240].rsplit(' ', 1)[0].rstrip(',;:') + '...'
    return out


def cf(x):
    t = '%g' % x
    return t + ('f' if '.' in t else '.f')


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    g = parse(GUIDE)
    if len(g) != 64:
        sys.exit('expected 64 factions, found %d' % len(g))
    old = json.load(open(os.path.join(ROOT, 'tools/guide_old.json')))
    tn = tech_table()
    rows, blurbs = [], []
    for key in KEYS + ['obsidian']:
        src = g.get(key if key != 'obsidian' else 'firmament')
        o = old[key]
        sec = src['sections']
        if key == 'obsidian':  # the Firmament's guide covers both; the Obsidian keep their old (fighting) table
            early, late, avoid = o['r1'], [], []
            core = o['core']
            te = []
        else:
            early, late, avoid = CARDS[key]
            core = techs_in(sec.get('Tech path', ''), tn)
            for t in o['core']:  # fill up from the old path
                if len(core) >= 8:
                    break
                if t not in core:
                    core.append(t)
            te = [t for t in techs_in(sec.get('Adjusted plan', '') + ' ' + sec.get("What changes with Thunder's Edge", ''), tn,
                                      False) if t not in core][:4]
        nums = list(o['nums'])  # dread cruiser destroyer fighter carrier infantry mech warSun | mecatol aggression expansion deals threat
        plan = ' '.join(sec.get(x, '') for x in ['Game plan', "Opening (rounds 1–2)", 'Mid and late game', 'Tech path']).lower()
        for idx, word in enumerate(['dreadnought', 'cruiser', 'destroyer', 'fighter', 'carrier', 'infantry', 'mech', 'war sun']):
            n = len(re.findall(r'\b' + word, plan))
            if n >= 3:
                nums[idx] = round(min(2.2, nums[idx] * (1.0 + 0.05 * min(n, 6))), 2)
        tags = src['tags'].lower() if key != 'obsidian' else 'military combat fracture'
        if re.search(r'military|aggression|combat|raiding|fleet|invasion|piracy', tags):
            nums[9] = round(min(1.4, nums[9] * 1.05), 2)
        if re.search(r'defensive|patient|diplomacy|peace', tags):
            nums[9] = round(max(0.7, nums[9] * 0.95), 2)
        if re.search(r'expansion|wide|exploration', tags):
            nums[10] = round(min(1.4, nums[10] * 1.05), 2)
        if re.search(r'trade|economy|arms dealer|corporate|debt', tags):
            nums[11] = round(min(1.6, nums[11] * 1.08), 2)
        if re.search(r'mecatol', tags):
            nums[8] = round(min(1.6, nums[8] * 1.1), 2)
        against = sec.get('Playing against them', '').lower()
        hit_early = 1.25 if re.search(r'\bearly\b|before (their|they)|snowball|rounds? 1', against) else 1.0
        exp = sec.get('Expedition priority', '')
        costs = []
        for pat, c in [('5 resources', 'res'), ('resources', 'res'), ('5 influence', 'inf'), ('influence', 'inf'),
                       ('3 trade goods', 'tg'), ('trade goods', 'tg'), ('action card', 'ac'), ('secret', 'secret'),
                       ('specialty', 'skip')]:
            p = exp.lower().find(pat)
            if p >= 0 and c not in [x[1] for x in costs]:
                costs.append((p, c))
        pay = [SLICE[c] for _, c in sorted(costs)][:3]
        m = re.search(r'round (\d)', exp)
        exp_from = int(m.group(1)) if m and key != 'obsidian' else 0
        rows.append((key, early, late, avoid, core[:8], o['agg'][:5], o['ctl'][:5], te, nums, pay, exp_from, hit_early,
                     TE.get(key, (0, 0, 0))))
        if key != 'obsidian':
            blurbs.append((key, src['difficulty'], src['tags'], blurb(sec.get('intro', ''), TOC[KEYS.index(key)])))
    for key, h in HAND.items():  # (0.83.0) Blue Reverie: by hand, after the guide's factions
        rows.append((key,) + tuple(h['row']))
        blurbs.append((key,) + tuple(h['blurb']))
    os.makedirs(os.path.join(ROOT, 'src/ai/gen'), exist_ok=True)
    with open(os.path.join(ROOT, 'src/ai/gen/guides.inc'), 'w') as f:
        f.write('// Generated by tools/gen_guides.py from docs/owner/TI4-Faction-Strategy-Guides.txt - do not edit.\n')
        f.write('const FactionGuide kGuides[] = {\n')
        for key, early, late, avoid, core, agg, ctl, te, nums, pay, exp_from, hit, tex in rows:
            arr = lambda xs, n, d='-1': '{' + ', '.join([str(x) for x in xs] + [d] * (n - len(xs))) + '}'
            sarr = lambda xs, n: '{' + ', '.join([cstr(x) for x in xs] + ['nullptr'] * (n - len(xs))) + '}'
            av = ' | '.join('(1 << %d)' % c for c in avoid) or '0'
            f.write('    {%s, %s, %s, %s,\n     %s,\n     %s, %s, %s,\n     %s,\n     %s, %d, %s, %s, %d, %s},\n' % (
                cstr(key), arr(early, 4), arr(late, 3), av, sarr(core, 8), sarr(agg, 5), sarr(ctl, 5), sarr(te, 4),
                ', '.join(cf(x) for x in nums), arr(pay, 3), exp_from, cf(hit), cf(tex[0]), tex[1], cf(tex[2])))
        f.write('};\n')
    os.makedirs(os.path.join(ROOT, 'src/app/gen'), exist_ok=True)
    with open(os.path.join(ROOT, 'src/app/gen/faction_blurbs.inc'), 'w') as f:
        f.write('// Generated by tools/gen_guides.py from docs/owner/TI4-Faction-Strategy-Guides.txt - do not edit.\n')
        f.write('const GuideBlurb kGuideBlurbs[] = {\n')
        for key, diff, tags, b in blurbs:
            f.write('    {%s, %s, %s, %s},\n' % (cstr(key), cstr(diff), cstr(tags), cstr(b)))
        f.write('};\n')
    print('guides: %d factions' % len(rows))


if __name__ == '__main__':
    main()
