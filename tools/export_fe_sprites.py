"""Front-end sprite (icon) table -> converted/frontend/fe_sprites.json (git-ignored game data).
    python tools/export_fe_sprites.py [out.json]
The table is filled at run time by the shape manager init (0x1F1580) and its pages are bound by 0x1F8C98;
both are run on the user's SLUS_203.26 (tools/lib/exe_tables.py).
"""
# FUN_001F1580 / FUN_001F8C98
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'frontend', 'fe_sprites.json')
sys.path.insert(0, os.path.join(here, 'lib'))
import exe_tables  # noqa: E402

PAGES = {
    'fe_1': 'FE_1/000_fe_1.png', 'fe_2': 'FE_1/001_fe_2.png', 'ps2c': 'FE_1/002_ps2c.png', 'dvd1': 'FE_1/003_dvd1.png',
    'gene': 'LOAD/000_gene.png',
}
PAGE_NAMES = ('gene', 'trik', 'cons', 'fe_1', 'fe_2', 'ps2c', 'dvd1')   # images bound by 0x1F8C98

recs = []
for sid, (page, w, h, v0, u0, u1, v1) in sorted(exe_tables.fe_sprite_sheet().items()):
    if page not in PAGE_NAMES:
        continue
    recs.append(dict(id=sid, page=page, w=w, h=h, u0=u0, v0=v0, u1=u1, v1=v1))   # record order {page, w, h, v0, u0, u1, v1}
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w') as f:
    json.dump(dict(source='fe_sprites.py (FUN_001F1580 / FUN_001F8C98)', pages=PAGES, sprites=recs), f, indent=1)
print('wrote %d sprites to %s' % (len(recs), out))
