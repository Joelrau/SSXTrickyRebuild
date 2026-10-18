"""In-race camera data DATA/CAMERA/*.CML -> Unreal/SSXTrickyRebuild/SimData/cameras/<file>.json (git-ignored).
    python tools/export_cameras.py [out_dir]
"""
# FUN_0016DAA8 / FUN_00176850 / FUN_001777d0 / FUN_0016e518
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'Unreal', 'SSXTrickyRebuild', 'SimData', 'cameras')
src = os.path.join(repo, 'extracted', 'DATA', 'CAMERA')
import cml_payload  # noqa: E402

DEAD = 0xDEADC0ED


def sint(w):
    return w - (1 << 32) if w & 0x80000000 else w


def flt(w):
    return struct.unpack('<f', struct.pack('<I', w))[0]


os.makedirs(out_dir, exist_ok=True)
for fn in sorted(os.listdir(src)):
    if not fn.upper().endswith('.CML'):
        continue
    doc = dict(source='DATA/CAMERA/' + fn, cameras={}, scripts={}, regions={}, paths={}, animations={}, scenes={})
    for r in cml_payload.decode(os.path.join(src, fn)):
        p = r['payload']
        if r['type'] == 'Camera':
            doc['cameras'][r['name']] = {k: v for k, v in p.items() if not k.startswith(('rt_', 'unk_0', 'unk_1'))
                                         or k == 'unk_16c'}
        elif r['type'] == 'DirectorScript':
            doc['scripts'][r['name']] = [{k: e[k] for k in ('opcode', 'name', 'a', 'b', 'c', 't0', 't1', 'arg')}
                                         for e in p['events']]
        elif r['type'] == 'CameraRegion':
            doc['regions'][r['name']] = p
        elif r['type'] == 'Path':
            doc['paths'][r['name']] = p
        elif r['type'] == 'Animation':
            codes = []
            for w in (int(x, 16) for x in p['raw']):
                if sint(w) < 0:
                    break
                codes.append(w)
            doc['animations'][r['name']] = codes
        elif r['type'] == 'Scene':
            words = [int(x, 16) for x in p['raw']]
            actors = []
            for k in range(8):
                a = words[12 * k:12 * k + 12]
                if len(a) < 12:
                    break
                name = '' if a[0] == DEAD else struct.pack('<4I', *a[:4]).split(b'\0')[0].decode('latin-1')
                actors.append(dict(name=name, enabled=0 if a[4] == DEAD else sint(a[4]), role=-1 if a[5] == DEAD else sint(a[5]),
                                   place=0 if a[6] == DEAD else sint(a[6]), path=0 if a[7] == DEAD else sint(a[7]),
                                   pos=[0.0 if w == DEAD else round(flt(w), 4) for w in a[8:11]],
                                   yaw=0.0 if a[11] == DEAD else round(flt(a[11]), 6)))
            doc['scenes'][r['name']] = actors
    out = os.path.join(out_dir, os.path.splitext(fn)[0].lower() + '.json')
    with open(out, 'w') as f:
        json.dump(doc, f, indent=1)
    print('%-13s cameras %3d scripts %3d regions %3d paths %2d animations %2d scenes %2d -> %s' % (
        fn, len(doc['cameras']), len(doc['scripts']), len(doc['regions']), len(doc['paths']), len(doc['animations']),
        len(doc['scenes']), out))

# FUN_001777d0 / FUN_00240ED8 / FUN_002550C8
sys.path.insert(0, here)
import pbd  # noqa: E402
from export_prop_sounds import map_instance_names  # noqa: E402
from pathlib import Path  # noqa: E402

areas = {}
for mp in sorted(Path(repo, 'unpacked', 'MODELS').glob('*/data/models/*.map')):
    stem = mp.stem.lower()
    pb = mp.with_suffix('.pbd')
    if not pb.is_file():
        continue
    by_name = {}
    for idx, name in sorted(map_instance_names(mp).items()):
        by_name.setdefault(name, idx)
    inst = None
    for key, name in (('1', 'Mdl_StageArea_Start_0'), ('2', 'Mdl_StageArea_Finish_0')):
        if name not in by_name:
            continue
        if inst is None:
            inst = pbd.load(pb).instances
        i = by_name[name]
        areas.setdefault(stem, {})[key] = dict(instance=i, matrix=list(inst[i].matrix))
out = os.path.join(out_dir, 'stage_areas.json')
with open(out, 'w') as f:
    json.dump(areas, f, indent=1)
print('stage areas: %d tracks -> %s' % (len(areas), out))
