"""Every faction's codec portrait: key -> (display name, set, function(t))."""
import sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import faces, faces_b1, faces_b2, faces_b3, faces_b4, faces_b5, faces_b6, faces_b7, faces_v2, faces_br  # noqa: E402

ORDER = [
    ('Base game', [('arborec', 'The Arborec', faces_b1), ('letnev', 'The Barony of Letnev', faces_b1),
                   ('saar', 'The Clan of Saar', faces_b1), ('muaat', 'The Embers of Muaat', faces_b1),
                   ('hacan', 'The Emirates of Hacan', faces_b1), ('sol', 'The Federation of Sol', faces_b1),
                   ('ghost', 'The Ghosts of Creuss', faces_b1), ('l1z1x', 'The L1Z1X Mindnet', faces_b1),
                   ('mentak', 'The Mentak Coalition', faces_b1), ('naalu', 'The Naalu Collective', faces_b2),
                   ('nekro', 'The Nekro Virus', faces_b2), ('sardakk', "Sardakk N'orr", faces_b2),
                   ('jolnar', 'The Universities of Jol-Nar', faces_b2), ('winnu', 'The Winnu', faces_b2),
                   ('xxcha', 'The Xxcha Kingdom', faces_b2), ('yin', 'The Yin Brotherhood', faces_b2),
                   ('yssaril', 'The Yssaril Tribes', faces_b2)]),
    ('Prophecy of Kings', [('argent', 'The Argent Flight', faces_b2), ('empyrean', 'The Empyrean', faces_b3),
                           ('mahact', 'The Mahact Gene-Sorcerers', faces), ('naaz', 'The Naaz-Rokha Alliance', faces_b3),
                           ('nomad', 'The Nomad', faces_b3), ('titans', 'The Titans of Ul', faces_b3),
                           ('cabal', "The Vuil'raith Cabal", faces_b3)]),
    ('Codex', [('keleres', 'The Council Keleres', faces_b3)]),
    ("Thunder's Edge", [('lastbastion', 'The Last Bastion', faces_b3), ('ralnel', 'The Ral Nel Consortium', faces_b3),
                        ('deepwrought', 'The Deepwrought Scholarate', faces_b3), ('crimson', 'The Crimson Rebellion', faces_b4),
                        ('firmament', 'The Firmament', faces_b4), ('obsidian', 'The Obsidian', faces_b4)]),
    ('Discordant Stars', [('axis', 'The Shipwrights of Axis', faces_b4), ('bentor', 'The Bentor Conglomerate', faces_b4),
                          ('kjalengard', 'The Berserkers of Kjalengard', faces_b5),
                          ('celdauri', 'The Celdauri Trade Confederation', faces_b4), ('cheiran', 'The Cheiran Hordes', faces_b4),
                          ('cymiae', 'The Savages of Cymiae', faces_b4), ('dihmohn', 'The Dih-Mohn Flotilla', faces_b4),
                          ('edyn', 'The Edyn Mandate', faces_b5), ('florzen', 'The Florzen Profiteers', faces_b5),
                          ('freesystems', 'The Free Systems Compact', faces_b5), ('ghemina', 'The Ghemina Raiders', faces_b5),
                          ('ghoti', 'The Ghoti Wayfarers', faces_b5), ('gledge', 'The Gledge Union', faces_b5),
                          ('ilyxum', 'The Augurs of Ilyxum', faces_b5), ('kollecc', 'The Kollecc Society', faces_b5),
                          ('kolume', 'The Monks of Kolume', faces_b6), ('kortali', 'The Kortali Tribunal', faces_b6),
                          ('kyro', 'The Kyro Sodality', faces_b6), ('lanefir', 'The Lanefir Remnants', faces_b6),
                          ('lizho', 'The Li-Zho Dynasty', faces_b6), ('ltokk', "The L'tokk Khrask", faces_b6),
                          ('mirveda', 'The Mirveda Protectorate', faces_b6), ('mortheus', 'The Glimmer of Mortheus', faces_b6),
                          ('myko', 'The Myko-Mentori', faces_b6), ('nivyn', 'The Nivyn Star Kings', faces),
                          ('nokar', 'The Nokar Sellships', faces_b7), ('olradin', 'The Olradin League', faces_b7),
                          ('rohdhna', "Roh'Dhna Mechatronics", faces_b7), ('tnelis', 'The Tnelis Syndicate', faces_b7),
                          ('vaden', 'The Vaden Banking Clans', faces_b7), ('vaylerian', 'The Vaylerian Scourge', faces_b7),
                          ('veldyr', 'The Veldyr Sovereignty', faces_b7), ('rhodun', 'The Zealots of Rhodun', faces_b7),
                          ('zelian', 'The Zelian Purifiers', faces_b7)]),
    # (0.83.0) Blue Reverie: each faction as it comes into the game (approved drafts in faces_br.py)
    ('Blue Reverie', [('atokera', 'The Atokera Legacy', faces_br), ('belkosea', 'The Belkosea Allied States', faces_br),
                      ('pharadn', "The Pharad'n Order", faces_br)]),
]


# round 2 (owner's pictures, approved): these keys use faces_v2
ROUND2 = {'arborec', 'axis', 'letnev', 'celdauri', 'saar', 'florzen', 'muaat', 'hacan', 'kortali', 'ghost', 'mirveda',
          'l1z1x', 'mortheus', 'mentak', 'tnelis', 'naalu', 'zelian', 'nekro', 'kjalengard', 'sardakk', 'kolume', 'winnu',
          'jolnar', 'yin', 'xxcha', 'mahact', 'empyrean', 'naaz', 'nomad', 'titans', 'crimson',
          'lastbastion', 'deepwrought', 'firmament', 'obsidian', 'ralnel'}


def fn(key, mod):
    if key in ROUND2:
        return getattr(faces_v2, key)
    return getattr(mod, key + '2' if mod is faces else key)


ALL = [(k, name, group, fn(k, mod)) for group, items in ORDER for k, name, mod in items]
