"""Front-end data tables from the executable -> converted/frontend/fe_tables.json (git-ignored game data).
    python tools/export_fe_tables.py [out.json]
"""
# 0x332148 / 0x332C88 / 0x379FD0 / 0x3B7F58
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'frontend', 'fe_tables.json')
sys.path.insert(0, os.path.join(here, 'lib'))
from slus import EXE, off, u32, i32, u8, cstr, board_table  # noqa: E402

RIDERS, OUTFITS, MODES = 12, 7, 12

boards = [dict(char=r['char'], slot=r['slot'], name=r['name'], index=r['index'], type=r['type'], cost=r['cost'],
               stats=r['stats']) for r in board_table()]

OUTFIT_BASE = 0x332C88
outfits = []
for c in range(RIDERS):
    row = []
    for o in range(OUTFITS):
        a = OUTFIT_BASE + (c * OUTFITS + o) * 8
        row.append(dict(name=cstr(u32(a)), value=i32(a + 4)))
    outfits.append(row)

venue_masks = [u32(0x379FD0 + 4 * m) for m in range(MODES)]

grid_off = off(0x3B7F58)
grid = EXE[grid_off:EXE.index(b'\0', grid_off)].decode('latin-1')
if len(grid) != 42:
    raise SystemExit('unexpected name grid length %d' % len(grid))

venue_order = [u8(0x3A8AF0 + i) for i in range(10)]

screen_names = [cstr(u32(0x347130 + 4 * i)) for i in range(10)]
map_path = os.path.join(repo, 'unpacked', 'MODELS', 'SSXFE', 'data', 'models', 'ssxfe.map')
map_inst, sec = {}, False
for line in open(map_path, encoding='latin-1'):
    if 'INTERNAL INSTANCES BEGIN' in line:
        sec = True
        continue
    if 'INTERNAL INSTANCES END' in line:
        break
    f = line.split()
    if sec and not line.startswith('#') and len(f) >= 2 and f[1].isdigit():
        map_inst.setdefault(f[0].lower(), int(f[1]))
venue_screens = [dict(name=n, instance=map_inst.get(n.lower(), -1)) for n in screen_names]
if any(v['instance'] < 0 for v in venue_screens):
    raise SystemExit('venue screen instance missing in %s: %s' % (map_path, venue_screens))

def named_instances(table):
    rows = [dict(name=n, instance=map_inst.get(n.lower(), -1)) for n in (cstr(u32(table + 4 * i)) for i in range(12))]
    if any(v['instance'] < 0 for v in rows):
        raise SystemExit('carousel instance missing in %s: %s' % (map_path, rows))
    return rows


track_names = [cstr(u32(0x332F28 + t * 0x1C + 4)) for t in range(13)]

def str_or_none(a):
    p = u32(a)
    return cstr(p) if p else None


dvd_talents = [str_or_none(0x348068 + 4 * i) for i in range(12)]
dvd_talents_full = [str_or_none(0x348038 + 4 * i) for i in range(12)]
dvd_char_names = [str_or_none(0x348098 + 4 * i) for i in range(12)]
dvd_char_order = [i32(0x3B2918 + 4 * i) for i in range(12)]
dvd_jukebox = [dict(title=cstr(u32(0x3480C8 + 12 * i)), artist=cstr(u32(0x3480C8 + 12 * i + 4)),
                    length=cstr(u32(0x3480C8 + 12 * i + 8))) for i in range(21)]

rider_signs = named_instances(0x3470C8)
platforms = named_instances(0x347100)
# 0x3470F8
marty_name = cstr(u32(0x3470F8))
marty_sign = dict(name=marty_name, instance=map_inst.get(marty_name.lower(), -1))

os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as f:
    json.dump(dict(source='SLUS_203.26 .data tables',
                   boards=boards, outfits=outfits, venue_masks=venue_masks, name_grid=grid,
                   venue_order=venue_order, venue_screens=venue_screens, rider_signs=rider_signs,
                   platforms=platforms, marty_sign=marty_sign, track_names=track_names, dvd_talents=dvd_talents, dvd_talents_full=dvd_talents_full,
                   dvd_char_names=dvd_char_names, dvd_char_order=dvd_char_order, dvd_jukebox=dvd_jukebox), f, indent=1)
print('wrote %d boards, %d outfit rows, %d venue masks, grid %d chars to %s'
      % (len(boards), len(outfits), len(venue_masks), len(grid), out))
