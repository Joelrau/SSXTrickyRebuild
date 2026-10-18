"""Trick naming / scoring tables -> converted/hud/ (git-ignored game data), read by Sim/Air/TrickName.cpp.
    python tools/export_trick_data.py [out_dir]
"""
# 0x320CF0 / 0x320F40 / 0x338120 / FUN_0019e7a0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import itertools
import json
import os
import random
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'hud')
E = open(os.path.join(repo, 'extracted', 'SLUS_203.26'), 'rb').read()
off = lambda va: va - 0x100000 + 0x1000
eb = lambda va: E[off(va)]
ef = lambda va: struct.unpack_from('<f', E, off(va))[0]

GRID_NORMAL, GRID_ALT, TIERS = 0x320CF0, 0x320F40, 0x338120


def grid(base):
    return [[eb(base + (r * 7 + c) * 4) for c in range(7)] for r in range(21)]


os.makedirs(out, exist_ok=True)
tiers = [dict(min=ef(TIERS + i * 20), a=ef(TIERS + i * 20 + 4), r=ef(TIERS + i * 20 + 8),
              g=ef(TIERS + i * 20 + 12), b=ef(TIERS + i * 20 + 16)) for i in range(6)]
grids = dict(source='ELF .data 0x320CF0 / 0x320F40 (FUN_00157490), 0x338120 (FUN_0019e7a0)',
             normal=grid(GRID_NORMAL), alt=grid(GRID_ALT), tiers=tiers)
json.dump(grids, open(os.path.join(out, 'trick_grids.json'), 'w'), indent=1)

import trick_name as tn  # noqa: E402

if tn.GRID_NORMAL != grids['normal'] or tn.GRID_ALT != grids['alt']:
    sys.exit('exported grids differ from models/trick_name.py')
rng = random.Random(20261003)
spins = list(range(-2160, 2161, 90)) + [89, -89, 91, -91, 269, -271, 1979, -1979]
flips = list(range(-1800, 1801, 90)) + [179, -181, 541, -539]
grab_sets = [[], [8], [8, 8], [12, 23], [1, 2, 3], [53], [26, 77]]
cases = []
for sp, fl in itertools.product(spins, flips):
    for _ in range(2):
        sw, ty, la, alt, tr, ls, sel = (rng.randint(0, 1), rng.choice((0, 0, 1, 2, 4)), rng.randint(0, 1),
                                        rng.randint(0, 1), rng.random() < 0.15, rng.randint(0, 1), rng.randint(0, 1))
        if ty:
            la = 0
        g = rng.choice(grab_sets)
        w0, w1, valid = tn.build_record(sp, fl, sw, ty, la, alt, g, to_rail=tr, land_switch=bool(ls),
                                        alt_select=bool(sel))
        cases.append(dict(spin=sp, flip=fl, switch=sw, type=ty, late=la, alt=alt, grabs=g, to_rail=int(tr),
                          land_switch=ls, alt_select=sel, w0=w0, w1=w1, valid=int(valid),
                          text=tn.trick_name(w0, w1)))
json.dump(dict(source='models/trick_name.py build_record / trick_name', cases=cases),
          open(os.path.join(out, 'trick_name_reference.json'), 'w'))
print('wrote trick_grids.json and %d reference cases to %s' % (len(cases), out))
