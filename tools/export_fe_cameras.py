"""Front-end scene cameras (ssxfe.pbd, 62 cameras) -> converted/frontend/fe_cameras.json (git-ignored).
    python tools/export_fe_cameras.py [out.json]
"""
# 0x26AC98
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'frontend', 'fe_cameras.json')
pbd = os.path.join(repo, 'unpacked', 'MODELS', 'SSXFE', 'data', 'models', 'ssxfe.pbd')
mapf = os.path.join(repo, 'unpacked', 'MODELS', 'SSXFE', 'data', 'models', 'ssxfe.map')
import fe_cameras  # noqa: E402

KEEP = ('posX', 'posY', 'posZ', 'rotX', 'rotY', 'rotZ', 'focal')
cams = []
for c in fe_cameras.load(pbd, mapf):
    name = c['name']
    if name.startswith('fecam_'):
        name = name[len('fecam_'):]
    if name.endswith('Shape'):
        name = name[:-len('Shape')]
    cams.append(dict(index=c['index'], name=name, duration=c['duration'], mask=c['channelMask'],
                     pos=list(c['pos']), rot=list(c['rot']), focal=c['focal'],
                     tracks={k: v for k, v in c['tracks'].items() if k in KEEP}))
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w') as f:
    json.dump(dict(source='ssxfe.pbd via fe_cameras.py', fov_constant=8.997951, cameras=cams), f, indent=1)
print('wrote %d cameras to %s' % (len(cams), out))
