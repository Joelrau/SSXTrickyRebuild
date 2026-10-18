"""In-race HUD data -> converted/hud/ (git-ignored game data), read by UI/SSXHudData.cpp.
    python tools/export_hud_data.py [out_dir]
The sprite sheet and the layout / boost tables are built by the executable at run time; they are produced
here by running its initialisers (tools/lib/exe_tables.py).
"""
# 0x33CA40 / 0x338008 / 0x338198 / 0x320970
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
sys.path.insert(0, os.path.join(here, 'lib'))
import exe_tables  # noqa: E402

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'hud')
E = exe_tables.exe_bytes()
cpu = exe_tables.hud_tables()
R = cpu.mem
rf = lambda a: struct.unpack_from('<f', R, a)[0]
ru = lambda a: struct.unpack_from('<I', R, a)[0]
eu = lambda va: struct.unpack_from('<I', E, va - 0x100000 + 0x1000)[0]


def estr(va):
    o = va - 0x100000 + 0x1000
    return E[o:E.index(b'\0', o)].decode('latin1')


os.makedirs(out, exist_ok=True)

sprites = []
for i, (page, w, h, v0, u0, u1, v1) in sorted(exe_tables.hud_sprite_sheet().items()):
    if page in ('map1', 'map4', 'hud1'):
        sprites.append(dict(id=i, page=page, w=w, h=h, u0=u0, v0=v0, u1=u1, v1=v1))
pages = {
    'HUDGAME': {'map1': 'HUDGAME/000_map1.png', 'map4': 'HUDGAME/001_map4.png', 'hud1': 'HUDGAME/002_hud1.png'},
    'HUDTRICK': {'map1': 'HUDTRICK/000_map1.png', 'map4': 'HUDTRICK/001_map4.png', 'hud1': 'HUDTRICK/002_hud1.png'},
}
json.dump(dict(source='RAM sheet', sheets=pages, sprites=sprites),
          open(os.path.join(out, 'hud_sprites.json'), 'w'), indent=1)

layouts = []
for lay in range(3):
    recs = []
    for r in range(53):
        a = 0x33CA40 + lay * 0x424 + r * 20
        recs.append(dict(x=rf(a), y=rf(a + 4), flags=ru(a + 8), sx=rf(a + 12), sy=rf(a + 16)))
    layouts.append(recs)
boost = []
for i in range(14):
    a = 0x338008 + i * 20
    boost.append(dict(lo=rf(a), hi=rf(a + 4), sprite=ru(a + 8), dx=rf(a + 12), dy=rf(a + 16)))
letters = []
for i in range(6):
    a = 0x338198 + i * 12
    letters.append(dict(rec=ru(a), dark=ru(a + 4), lit=ru(a + 8)))
json.dump(dict(source='RAM .data 0x33CA40 / 0x338008 / 0x338198', layouts=layouts, boost=boost,
               letters=letters), open(os.path.join(out, 'hud_layout.json'), 'w'), indent=1)

BASE, END = 0x320970, 0x320CF0   # through GRAB[101] "Combo Grab" at 0x320CEC
slots = []
for va in range(BASE, END, 4):
    p = eu(va)
    slots.append(estr(p) if 0x100000 <= p < 0x100000 + len(E) else None)
# 0x28EA78 / 0x28EA70
units = dict(kmh=estr(0x28EA78 - 0x1000 + 0x100000), mph=estr(0x28EA70 - 0x1000 + 0x100000))
json.dump(dict(source='ELF .data 0x320970..0x320CF0 (FUN_001551c0)', base=BASE, slots=slots, units=units),
          open(os.path.join(out, 'trick_names.json'), 'w'), indent=1)
print('wrote %d sprites, 3x53 layout records, %d trick-name slots to %s' % (len(sprites), len(slots), out))
